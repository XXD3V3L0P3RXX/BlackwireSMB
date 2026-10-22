#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "reader_source.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#ifndef BW_READER_SOURCE_HOST
#include <fcntl.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#endif

static int rs_path(struct bw_reader_source *p,const char *alias,char out[2048]){
    if(!p||!p->_ready||p->_failed||!alias)return -1;
    size_t prefix=strlen(p->_binding.game),length=strnlen(alias,BW_FILES_PATH_CAPACITY);
    if(length==BW_FILES_PATH_CAPACITY||length<=prefix+1||memcmp(alias,p->_binding.game,prefix)||alias[prefix]!='/')goto bad;
    const char *relative=alias+prefix+1;
    if(bw_manifest_validate_path(relative,strlen(relative)))goto bad;
    int n=snprintf(out,2048,"%s/%s",p->_binding.source,relative);
    if(n<0||n>=2048)goto bad;
    return 0;
bad:
    p->_failed=1;return -1;
}
static int rs_stat(void *opaque,const char *alias,struct bw_files_remote_stat *out){
    struct bw_reader_source *p=opaque;char path[2048];
    return rs_path(p,alias,path)?-1:p->_ops.stat(p->_context,path,out);
}
static void *rs_open(void *opaque,const char *alias){
    struct bw_reader_source *p=opaque;char path[2048];
    return rs_path(p,alias,path)?NULL:p->_ops.open_readonly(p->_context,path);
}
static int rs_fstat(void *opaque,void *handle,struct bw_files_remote_stat *out){
    struct bw_reader_source *p=opaque;
    return !p||!p->_ready||p->_failed?-1:p->_ops.fstat(p->_context,handle,out);
}
static int rs_close(void *opaque,void *handle){
    struct bw_reader_source *p=opaque;
    /* Always consume already-owned handles, including after a path failure. */
    return p->_ops.close(p->_context,handle);
}
int bw_reader_source_open(struct bw_reader_source *p,struct bw_files *files,
    const struct bw_share_config *config,const struct bw_source_binding *binding,
    const struct bw_files_remote_ops *ops,void *context){
    if(!p||p->_ready||p->_failed||!files||!files->manifest.count||files->_remote_bound||files->handle_count||
       !binding||!ops||!ops->stat||!ops->open_readonly||!ops->fstat||!ops->close||!context)return -1;
    struct bw_source_binding checked;
    if(!memchr(binding->source,0,sizeof binding->source)||!memchr(binding->game,0,sizeof binding->game)||
       !memchr(binding->title,0,sizeof binding->title)||bw_source_binding_resolve(&checked,config,binding->source,binding->title)||
       strcmp(checked.game,binding->game)||memcmp(checked.sha256,binding->sha256,32))return -1;
    const char *leaf=strrchr(files->_root,'/');
    if(!leaf||strcmp(leaf+1,checked.game))return -1;
    p->_binding=checked;p->_files=files;p->_ops=*ops;p->_context=context;p->_ready=1;return 0;
}
void *bw_reader_source_get(struct bw_reader_source *p,uint32_t id){
    static const struct bw_files_remote_ops ops={rs_stat,rs_open,rs_fstat,rs_close};
    if(!p||!p->_ready||p->_failed)return NULL;
    return bw_files_remote_get_ops(p->_files,&ops,p,id,p->_binding.game);
}
#ifndef BW_READER_SOURCE_HOST
static void rs_smb_stat_copy(struct bw_files_remote_stat *out,const struct smb2_stat_64 *st){
    *out=(struct bw_files_remote_stat){st->smb2_size,st->smb2_mtime,st->smb2_ino,st->smb2_type==SMB2_TYPE_FILE};
}
static int rs_smb_stat(void *context,const char *path,struct bw_files_remote_stat *out){
    struct smb2_stat_64 st;if(smb2_stat(context,path,&st))return -1;rs_smb_stat_copy(out,&st);return 0;
}
static void *rs_smb_open(void *context,const char *path){return smb2_open(context,path,O_RDONLY);}
static int rs_smb_fstat(void *context,void *handle,struct bw_files_remote_stat *out){
    struct smb2_stat_64 st;if(smb2_fstat(context,handle,&st))return -1;rs_smb_stat_copy(out,&st);return 0;
}
static int rs_smb_close(void *context,void *handle){return smb2_close(context,handle);}
int bw_reader_source_smb_open(struct bw_reader_source *p,struct bw_files *files,
    const struct bw_share_config *config,const struct bw_source_binding *binding,struct smb2_context *smb){
    static const struct bw_files_remote_ops ops={rs_smb_stat,rs_smb_open,rs_smb_fstat,rs_smb_close};
    return bw_reader_source_open(p,files,config,binding,&ops,smb);
}
#endif


#include "derived_provenance.h"
#include "derived_batch_provenance.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
struct dr_state {
    struct bw_files *files;const void *map_bytes;size_t map_size;
    struct bw_manifest manifest,base_manifest;
    char root[BW_FILES_PATH_CAPACITY];
    struct bw_derived_provenance history;
    const struct bw_derived_provenance_facts *facts;
    struct bw_batch_provenance batch_history;
    const struct bw_batch_facts *batch_facts;
    struct bw_derived_reader_claim claim,captured;
    struct bw_files_remote_ops ops;void *context;
    bw_derived_reader_current_fn current;void *opaque;
    uint32_t active_id;unsigned in_get,in_close,close_failed;
};
_Static_assert(sizeof(struct dr_state)<8192,"derived reader fixed-state bound");
static int dr_fail(struct bw_derived_reader_source *p,const char *why){if(p){p->failed=1;p->error=why;}return -1;}
static int dr_hex(uint8_t out[32],const char *s){if(!memchr(s,0,65)||strlen(s)!=64)return -1;for(unsigned i=0;i<32;i++){unsigned value=0;for(unsigned j=0;j<2;j++){const char *at=strchr("0123456789abcdef",s[2*i+j]);if(!at)return -1;value=value*16u+(unsigned)(at-"0123456789abcdef");}out[i]=(uint8_t)value;}return 0;}
static int dr_same_ops(const struct bw_files_remote_ops *a,const struct bw_files_remote_ops *b){return a->stat==b->stat&&a->open_readonly==b->open_readonly&&a->fstat==b->fstat&&a->close==b->close;}
static int dr_close(void *,void *);
static int dr_current(struct bw_derived_reader_source *p){
    if(!p||p->failed||!p->state)return -1;struct dr_state *s=p->state;
    if(memcmp(&s->claim,&s->captured,sizeof s->claim)||!s->current||s->current(s->opaque,&s->claim)||p->failed||memcmp(&s->claim,&s->captured,sizeof s->claim))return dr_fail(p,"derived_reader_lease_refused");return 0;
}
static int dr_map(struct bw_derived_reader_source *p){
    if(dr_current(p))return -1;struct dr_state *s=p->state;size_t n=0;const void *bytes=bw_files_manifest_bytes(s->files,&n);
    const void *expected=s->batch_facts?s->batch_facts->map:s->facts->plan.map;
    if(bytes!=s->map_bytes||n!=s->map_size||memcmp(&s->files->manifest,&s->manifest,sizeof s->manifest)||
       !memchr(s->files->_root,0,sizeof s->files->_root)||strcmp(s->files->_root,s->root)||memcmp(bytes,expected,n))return dr_fail(p,"derived_reader_map_changed");return 0;
}
static int dr_row(struct bw_derived_reader_source *p,uint32_t id,struct bw_manifest_record *row){
    struct dr_state *s=p->state;uint32_t count=s->batch_facts?s->batch_facts->count:s->facts->plan.count,original_id;
    if(id>=count||bw_manifest_get(&s->manifest,id,row)||row->flags!=BW_MANIFEST_STREAM)return dr_fail(p,"derived_reader_not_original_stream");
    if(s->batch_facts){
        const struct bw_batch_origin *o=s->batch_facts->origins+id;
        if(o->file_id!=id||o->kind!=BW_BATCH_ORIGINAL||o->archive_index!=UINT32_MAX||o->entry_index!=UINT32_MAX)
            return dr_fail(p,"batch_reader_not_original_stream");
        original_id=o->original_id;
    }else{
        const struct bw_derived_origin *o=s->facts->plan.origins+id;
        if(o->file_id!=id||o->kind!=BW_DERIVED_ORIGINAL||o->archive_original_id!=UINT32_MAX||o->recipe_index!=UINT32_MAX)
            return dr_fail(p,"derived_reader_origin_changed");
        original_id=o->original_id;
    }
    struct bw_manifest_record original;
    if(bw_manifest_get(&s->base_manifest,original_id,&original)||
       original.flags!=BW_MANIFEST_STREAM||strcmp(row->path,original.path)||row->size!=original.size||row->mtime_sec!=original.mtime_sec)return dr_fail(p,"derived_reader_origin_changed");return 0;
}
static int dr_path(struct bw_derived_reader_source *p,const char *alias,char out[2048]){
    if(!p||!p->busy||p->failed||!p->state)return -1;struct dr_state *s=p->state;struct bw_manifest_record row;
    if(!s->in_get||!alias||dr_map(p)||dr_row(p,s->active_id,&row))return dr_fail(p,"derived_reader_alias_outside_get");
    size_t n=strnlen(alias,BW_FILES_PATH_CAPACITY),prefix=strlen(s->claim.prepared_game),relative=strlen(row.path);
    if(n==BW_FILES_PATH_CAPACITY||n!=prefix+1+relative||memcmp(alias,s->claim.prepared_game,prefix)||alias[prefix]!='/'||memcmp(alias+prefix+1,row.path,relative+1))return dr_fail(p,"derived_reader_foreign_alias");
    int wrote=snprintf(out,2048,"%s/%s",s->claim.original.binding.source,row.path);if(wrote<0||wrote>=2048)return dr_fail(p,"derived_reader_remote_path_bound");return 0;
}
static int dr_stat(void *raw,const char *alias,struct bw_files_remote_stat *out){struct bw_derived_reader_source *p=raw;char path[2048];if(dr_path(p,alias,path))return -1;struct dr_state *s=p->state;int result=s->ops.stat(s->context,path,out);return dr_current(p)||result?-1:0;}
static void *dr_open(void *raw,const char *alias){struct bw_derived_reader_source *p=raw;char path[2048];if(dr_path(p,alias,path))return NULL;struct dr_state *s=p->state;void *h=s->ops.open_readonly(s->context,path);if(dr_current(p)){if(h)(void)dr_close(p,h);return NULL;}return h;}
static int dr_fstat(void *raw,void *handle,struct bw_files_remote_stat *out){struct bw_derived_reader_source *p=raw;if(!p||!p->busy||!p->state||dr_map(p))return -1;struct dr_state *s=p->state;if(!s->in_get)return dr_fail(p,"derived_reader_fstat_outside_get");int result=s->ops.fstat(s->context,handle,out);return dr_current(p)||result?-1:0;}
static int dr_close(void *raw,void *handle){
    struct bw_derived_reader_source *p=raw;if(!p||!p->state)return -1;struct dr_state *s=p->state;
    if(s->in_close)return dr_fail(p,"derived_reader_close_reentry");unsigned prior=p->busy;p->busy=1;s->in_close=1;int result=s->ops.close(s->context,handle);s->in_close=0;p->busy=prior;if(result){s->close_failed=1;dr_fail(p,"derived_reader_close_failed");}return result;
}
static const struct bw_files_remote_ops dr_ops={dr_stat,dr_open,dr_fstat,dr_close};
int bw_derived_reader_source_open(struct bw_derived_reader_source *p,struct bw_files *files,const struct bw_share_config *config,const struct bw_derived_reader_request *r,const struct bw_files_remote_ops *ops,void *context){
    if(!p)return -1;if(p->state||p->busy||p->failed)return dr_fail(p,"derived_reader_reuse");
    size_t provenance_limit=r&&r->selection&&bw_reader_selection_is_batch(r->selection)?BW_BATCH_PROVENANCE_LIMIT:BW_DERIVED_PROVENANCE_LIMIT;
    if(!files||!config||!r||!r->selection||!r->provenance||!r->provenance_size||r->provenance_size>provenance_limit||!r->admitted_receipt_sha256||!r->current||!r->opaque||!ops||!ops->stat||!ops->open_readonly||!ops->fstat||!ops->close||!context||files->_remote_bound||files->handle_count)return dr_fail(p,"derived_reader_arguments");
    char canonical[BW_READER_SELECTION_BYTES+1];size_t selected_size=0;struct bw_reader_selection selected;
    if((!bw_reader_selection_is_derived(r->selection)&&!bw_reader_selection_is_batch(r->selection))||bw_reader_selection_serialize(r->selection,canonical,sizeof canonical,&selected_size)||bw_reader_selection_parse(&selected,canonical,selected_size))return dr_fail(p,"derived_reader_selection");
    struct dr_state *s=calloc(1,sizeof *s);if(!s)return dr_fail(p,"derived_reader_allocation");p->state=s;p->busy=1;s->files=files;s->ops=*ops;s->context=context;s->current=r->current;s->opaque=r->opaque;
    struct bw_source_binding original;uint8_t expected[32];int bad=bw_source_binding_resolve(&original,config,selected.source,selected.title)||strcmp(original.game,selected.game)||dr_hex(expected,selected.binding_hex)||memcmp(original.sha256,expected,32);
    if(!bad){cp_sha_bytes(r->provenance,r->provenance_size,s->claim.provenance_sha256);bad=dr_hex(expected,selected.provenance_hex)||memcmp(expected,s->claim.provenance_sha256,32)||dr_hex(expected,selected.prepared_record_hex)||memcmp(expected,r->admitted_receipt_sha256,32);
        if(!bad)bad=bw_reader_selection_is_batch(&selected)?bw_batch_provenance_decode(&s->batch_history,r->provenance,r->provenance_size):bw_derived_provenance_decode(&s->history,r->provenance,r->provenance_size);}
    if(!bad){
        if(bw_reader_selection_is_batch(&selected)){s->batch_facts=bw_batch_provenance_view(&s->batch_history);bad=!s->batch_facts;
            if(!bad)bad=bw_manifest_open(&s->base_manifest,s->batch_facts->base_map,s->batch_facts->base_map_size)!=BW_MANIFEST_OK;}
        else{s->facts=bw_derived_provenance_view(&s->history);bad=!s->facts;
            if(!bad)bad=bw_manifest_open(&s->base_manifest,s->facts->base_map,s->facts->base_map_size)!=BW_MANIFEST_OK;}
    }
    size_t map_size=0;const void *map=bw_files_manifest_bytes(files,&map_size);
    if(!bad){
        const struct bw_provider_binding *identity=s->batch_facts?&s->batch_facts->original:&s->facts->original;
        const struct bw_derived_prepared_identity *prepared=s->batch_facts?&s->batch_facts->prepared:&s->facts->plan.identity;
        const void *expected_bytes=s->batch_facts?s->batch_facts->map:s->facts->plan.map;
        size_t expected_size=s->batch_facts?s->batch_facts->map_size:s->facts->plan.map_size;
        uint32_t count=s->batch_facts?s->batch_facts->count:s->facts->plan.count;
        const uint8_t *map_hash=s->batch_facts?s->batch_facts->map_sha256:s->facts->plan.map_sha256;
        const char *leaf=NULL;
        bad=identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=1||memcmp(&identity->binding,&original,sizeof original)||prepared->policy!=selected.prepared_policy||strcmp(prepared->component,selected.prepared_game)||!map||map_size!=expected_size||files->manifest.count!=selected.manifest_count||files->manifest.checksum!=selected.manifest_hash||memcmp(map,expected_bytes,map_size)||!memchr(files->_root,0,sizeof files->_root);
        if(!bad){leaf=strrchr(files->_root,'/');bad=!leaf||strcmp(leaf+1,selected.prepared_game);}
        struct bw_manifest expected_map;if(!bad){bad=bw_manifest_open(&expected_map,map,map_size)!=BW_MANIFEST_OK;if(!bad)bad=expected_map._records!=files->manifest._records||expected_map._paths!=files->manifest._paths||expected_map.count!=files->manifest.count||expected_map.path_bytes!=files->manifest.path_bytes||expected_map.checksum!=files->manifest.checksum;}
        struct bw_manifest_record boot;unsigned found=0;for(uint32_t i=0;!bad&&i<count;i++){if(bw_manifest_get(&files->manifest,i,&boot)){bad=1;break;}if(!strcmp(boot.path,"eboot.bin")){found++;if(boot.flags!=BW_MANIFEST_LOCAL||boot.size!=selected.eboot_size)bad=1;}}if(!bad&&found!=1)bad=1;
        if(!bad){s->map_bytes=map;s->map_size=map_size;s->manifest=files->manifest;memcpy(s->root,files->_root,strlen(files->_root)+1);s->claim.original=*identity;memcpy(s->claim.prepared_game,selected.prepared_game,75);memcpy(s->claim.run,selected.run,33);s->claim.prepared_policy=selected.prepared_policy;s->claim.manifest_count=selected.manifest_count;s->claim.manifest_hash=selected.manifest_hash;s->claim.eboot_size=selected.eboot_size;memcpy(s->claim.receipt_sha256,r->admitted_receipt_sha256,32);memcpy(s->claim.map_sha256,map_hash,32);memcpy(s->claim.commitment_sha256,prepared->commitment_sha256,32);s->captured=s->claim;bad=dr_map(p);}
    }
    p->busy=0;if(bad)return dr_fail(p,"derived_reader_admission_refused");return 0;
}
void *bw_derived_reader_source_get(struct bw_derived_reader_source *p,uint32_t id){
    if(!p||!p->state||p->failed)return NULL;if(p->busy){dr_fail(p,"derived_reader_reentry");return NULL;}p->busy=1;struct dr_state *s=p->state;struct bw_manifest_record row;void *handle=NULL;
    if(!dr_map(p)&&!dr_row(p,id,&row)){
        if(s->files->_remote_bound&&(s->files->_remote_context!=p||strcmp(s->files->_prefix,s->claim.prepared_game)||!dr_same_ops(&s->files->_ops,&dr_ops)))dr_fail(p,"derived_reader_foreign_pool");
        else{s->active_id=id;s->in_get=1;handle=bw_files_remote_get_ops(s->files,&dr_ops,p,id,s->claim.prepared_game);s->in_get=0;if(!handle||dr_map(p))dr_fail(p,"derived_reader_remote_refused");}
    }
    p->busy=0;return p->failed?NULL:handle;
}
int bw_derived_reader_source_destroy(struct bw_derived_reader_source *p){
    if(!p)return -1;if(p->busy)return dr_fail(p,"derived_reader_destroy_busy");int close_failed=0;if(p->state){struct dr_state *s=p->state;if(s->files->handle_count||s->files->_remote_bound)return dr_fail(p,"derived_reader_destroy_map_first");close_failed=s->close_failed!=0;bw_derived_provenance_destroy(&s->history);bw_batch_provenance_destroy(&s->batch_history);free(s);}memset(p,0,sizeof *p);return close_failed?-1:0;
}
#ifndef BW_READER_SOURCE_HOST
int bw_derived_reader_source_smb_open(struct bw_derived_reader_source *p,struct bw_files *files,const struct bw_share_config *config,const struct bw_derived_reader_request *r,struct smb2_context *smb){static const struct bw_files_remote_ops ops={rs_smb_stat,rs_smb_open,rs_smb_fstat,rs_smb_close};return bw_derived_reader_source_open(p,files,config,r,&ops,smb);}
#endif
