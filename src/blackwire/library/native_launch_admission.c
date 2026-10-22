/* Included only by native_send.c: reuse the exact native receipt, normalized
 * *at calls and checked index codec. It is not an independently linked TU. */
#include "native_launch_admission.h"
#include "source_param.h"

#define NA_REQUIRED_LIMIT 256u
#define NA_META_LIMIT 8u
struct na_file {
    int fd,parent,owned_parent,root;
    char leaf[256],relative[1024];
    struct stat initial;
    uint32_t row;
};
struct bw_native_launch_admission {
    struct bw_provider_binding identity;
    struct bw_native_prepared prepared;
    struct bw_reader_selection selected;
    struct prepare_v2 anchors[4];
    int directories[4];
    struct stat directory_ids[4];
    struct na_file files[NA_META_LIMIT+NA_REQUIRED_LIMIT];
    unsigned file_count,required_count;
    uint8_t *map,*digests,*index;
    size_t map_size,digest_size,index_size;
    struct bw_manifest manifest;
    uint8_t generation[32],map_sha256[32];
    int (*guard)(void *);
    void *opaque;
    double deadline;
    int valid,failed,source_checked;
    const char *error;
};
static int na_fail(struct bw_native_launch_admission *p,const char *message){
    if(p){p->failed=1;if(!p->error)p->error=message;}return -1;
}
static int na_same(const struct stat *a,const struct stat *b){
    return cp_measure_same(a,b)&&a->st_uid==b->st_uid&&a->st_gid==b->st_gid;
}
static int na_guard(struct bw_native_launch_admission *p){
    double now=cp_now();
    return !p||p->failed||!p->guard||now<0||now>=p->deadline||p->guard(p->opaque)?
        na_fail(p,"The launch selection or settings changed; choose Send to revalidate startup files."):0;
}
static int na_bound(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
    return a&&b&&a->kind==BW_PROVIDER_FOLDER&&b->kind==BW_PROVIDER_FOLDER&&a->policy==BW_PROVIDER_POLICY_V1&&b->policy==a->policy&&
        !memcmp(a->binding.sha256,b->binding.sha256,32)&&
        memchr(b->binding.source,0,sizeof b->binding.source)&&memchr(b->binding.game,0,sizeof b->binding.game)&&memchr(b->binding.title,0,sizeof b->binding.title)&&
        !strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.title,b->binding.title);
}
static int na_selected(struct bw_native_launch_admission *p,const struct bw_reader_selection *s){
    char a[BW_READER_SELECTION_BYTES+1],b[BW_READER_SELECTION_BYTES+1];size_t an=0,bn=0;
    return !s||bw_reader_selection_serialize(&p->selected,a,sizeof a,&an)||bw_reader_selection_serialize(s,b,sizeof b,&bn)||an!=bn||memcmp(a,b,an)?-1:0;
}
static int na_file_current(struct bw_native_launch_admission *p,struct na_file *f){
    struct stat held;
    if(f->root<0||f->root>3||!v2_directory_current(&p->anchors[f->root],p->directories[f->root],f->parent,f->relative)||
       cp_regular(f->parent,f->leaf,f->fd,(uint64_t)f->initial.st_size,&held)||!na_same(&f->initial,&held))return -1;
    return 0;
}
static int na_current(struct bw_native_launch_admission *p){
    if(na_guard(p))return -1;
    for(unsigned i=0;i<4;i++){
        struct stat held;
        if(p->directories[i]<0||!v2_root_current(&p->anchors[i],p->directories[i])||fstat(p->directories[i],&held)||
           held.st_dev!=p->directory_ids[i].st_dev||held.st_ino!=p->directory_ids[i].st_ino||
           held.st_mode!=p->directory_ids[i].st_mode||held.st_uid!=p->directory_ids[i].st_uid||held.st_gid!=p->directory_ids[i].st_gid)
            return na_fail(p,"The prepared startup directory changed; choose Send to repair it.");
    }
    for(unsigned i=0;i<p->file_count;i++)if(na_file_current(p,&p->files[i]))
        return na_fail(p,"A required startup file or verified receipt changed; choose Send to repair it.");
    return na_guard(p);
}
static int na_add(struct bw_native_launch_admission *p,unsigned root,const char *path,size_t maximum,void **bytes,size_t *size,uint32_t row){
    if(bytes)*bytes=NULL;if(size)*size=0;
    if(root>3||p->file_count>=NA_META_LIMIT+NA_REQUIRED_LIMIT||na_guard(p))return -1;
    struct na_file f={.fd=-1,.parent=-1,.root=(int)root,.row=row};
    f.parent=v2_parent(&p->anchors[root],p->directories[root],path,0,f.leaf,f.relative);
    if(f.parent<0)return -1;f.owned_parent=1;
    f.fd=v2_openat(f.parent,f.leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    void *body=NULL;int result=-1;
    if(f.fd<0||fstat(f.fd,&f.initial)||!S_ISREG(f.initial.st_mode)||f.initial.st_nlink!=1||f.initial.st_size<0||
       (uint64_t)f.initial.st_size>maximum||na_file_current(p,&f))goto done;
    if(bytes){
        size_t length=(size_t)f.initial.st_size;body=malloc(length?length:1);if(!body)goto done;
        size_t at=0;while(at<length){if(na_guard(p))goto done;size_t want=length-at<65536?length-at:65536;
            ssize_t n=pread(f.fd,(uint8_t *)body+at,want,(off_t)at);if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want)goto done;at+=(size_t)n;}
        if(na_file_current(p,&f))goto done;
    }
    p->files[p->file_count++]=f;
    if(bytes){*bytes=body;body=NULL;if(size)*size=(size_t)f.initial.st_size;}result=0;
done:
    free(body);if(result){if(f.fd>=0)close(f.fd);if(f.parent>=0)close(f.parent);}return result;
}
static int na_exact(struct bw_native_launch_admission *p,unsigned root,const char *path,const void *expected,size_t size){
    void *body=NULL;size_t got=0;int bad=na_add(p,root,path,size,&body,&got,UINT32_MAX)||got!=size||(got==size&&memcmp(body,expected,size));free(body);return bad?-1:0;
}
static int na_required(const struct bw_manifest_record *row){
    return row->flags==BW_MANIFEST_LOCAL&&(!strcmp(row->path,"eboot.bin")||!strcmp(row->path,"sce_sys/param.json")||(bw_startup_mode(row->path)&0111));
}
static int na_hash_required(struct bw_native_launch_admission *p,uint32_t id){
    struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,id,&row)||p->required_count==NA_REQUIRED_LIMIT)return -1;
    if(na_add(p,3,row.path,(size_t)row.size,NULL,NULL,id))return -1;
    struct na_file *f=&p->files[p->file_count-1];
    if((uint64_t)f->initial.st_size!=row.size||(uint64_t)f->initial.st_mtime!=cp_u64(p->index+48u+24u*id+16)||
       (f->initial.st_mode&07777)!=bw_startup_mode(row.path))return -1;
    uint8_t buffer[65536],hash[32];struct cp_sha sha;cp_sha_init(&sha);
    for(uint64_t at=0;at<row.size;){
        if(na_guard(p))return -1;size_t want=row.size-at<sizeof buffer?(size_t)(row.size-at):sizeof buffer;
        BW_LT_COUNT(BW_LT_LOCAL_READ_CALLS,1);ssize_t n=pread(f->fd,buffer,want,(off_t)at);
        if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>want)return -1;
        cp_sha_update(&sha,buffer,(size_t)n);BW_LT_COUNT(BW_LT_LOCAL_HASH_BYTES,(uint64_t)n);at+=(uint64_t)n;
    }
    BW_LT_COUNT(BW_LT_LOCAL_HASH_CALLS,1);cp_sha_final(&sha,hash);
    if(memcmp(hash,p->digests+56u+32u*id,32)||na_file_current(p,f))return -1;
    p->required_count++;return 0;
}
int bw_native_launch_admission_close(struct bw_native_launch_admission **owner){
    if(!owner)return -1;struct bw_native_launch_admission *p=*owner;*owner=NULL;if(!p)return 0;int bad=0;
    for(unsigned i=0;i<p->file_count;i++){
        if(p->files[i].fd>=0&&close(p->files[i].fd))bad=1;
        if(p->files[i].owned_parent&&p->files[i].parent>=0&&close(p->files[i].parent))bad=1;
    }
    for(unsigned i=0;i<4;i++)if(p->directories[i]>=0&&close(p->directories[i]))bad=1;
    free(p->map);free(p->digests);free(p->index);free(p);return bad?-1:0;
}
int bw_native_launch_admission_open(struct bw_native_launch_admission **out,const char *base,
    const struct bw_provider_binding *identity,const struct bw_native_prepared *expected,const char *run,
    int (*guard)(void *),void *opaque,const char **error){
    if(error)*error="The verified startup receipt is unavailable; choose Send to repair it.";
    if(!out||*out||!base||!identity||identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=BW_PROVIDER_POLICY_V1||
       !expected||expected->state!=BW_PREPARED_VALID||expected->prepared_policy||expected->startup_upgrade_required||!ns_nonce(run)||!guard)return -1;
    if(pthread_mutex_trylock(&ns_mutex))return -1;
    struct bw_native_launch_admission *p=calloc(1,sizeof *p);int result=-1;struct bw_json json={.maximum=4096};
    uint8_t *record=NULL;size_t size=0;char (*paths)[2048]=calloc(4,2048);struct cp_plan *plan=calloc(1,sizeof *plan);
    if(p)for(unsigned i=0;i<4;i++)p->directories[i]=-1;
    if(!p||!paths||!plan)goto done;
    p->identity=*identity;p->guard=guard;p->opaque=opaque;p->deadline=cp_now()+300.0;
    if(na_guard(p)||ns_paths(base,&identity->binding,paths[2])||ns_provider_json(identity,&json))goto done;
    int a=snprintf(paths[0],sizeof paths[0],"%s",base),b=snprintf(paths[1],sizeof paths[1],"%s/native-prepared",base),
        c=snprintf(paths[3],sizeof paths[3],"%s/games/%s",base,identity->binding.game);
    if(a<0||(size_t)a>=sizeof paths[0]||b<0||(size_t)b>=sizeof paths[1]||c<0||(size_t)c>=sizeof paths[3])goto done;
    for(unsigned i=0;i<4;i++)if((p->directories[i]=ns_root(paths[i],&p->anchors[i]))<0||fstat(p->directories[i],&p->directory_ids[i]))goto done;
    if(na_exact(p,1,".blackwire-native-prepared",ns_scope,sizeof ns_scope-1)||na_exact(p,2,"source.json",json.bytes,json.used)||
       na_exact(p,3,"blackwire.generation.owner",json.bytes,json.used)||na_add(p,2,"prepared.bin",NS_RECORD_SIZE,(void **)&record,&size,UINT32_MAX)||
       size!=NS_RECORD_SIZE||ns_record_valid(record,&identity->binding)||(p->files[p->file_count-1].initial.st_mode&07777)!=0600)goto done;
    uint8_t hash[32];cp_sha_bytes(record,size,hash);
    if(memcmp(hash,expected->record_sha256,32)||(uint64_t)p->directory_ids[3].st_dev!=cp_u64(record+304)||
       (uint64_t)p->directory_ids[3].st_ino!=cp_u64(record+312)||(uint64_t)p->directory_ids[2].st_dev!=cp_u64(record+320)||
       (uint64_t)p->directory_ids[2].st_ino!=cp_u64(record+328))goto done;
    p->map_size=(size_t)cp_u64(record+280);p->digest_size=(size_t)cp_u64(record+288);p->index_size=(size_t)cp_u64(record+296);
    const char *names[]={"blackwire.files","blackwire.generation.digests","ampr_emu.index"};
    void **bodies[]={(void **)&p->map,(void **)&p->digests,(void **)&p->index};size_t sizes[]={p->map_size,p->digest_size,p->index_size};
    for(unsigned i=0;i<3;i++){
        if(na_add(p,3,names[i],sizes[i],bodies[i],&size,UINT32_MAX)||size!=sizes[i])goto done;
        cp_sha_bytes(*bodies[i],size,hash);if(memcmp(hash,record+112+32u*i,32))goto done;
    }
    if(bw_manifest_open(&p->manifest,p->map,p->map_size))goto done;
    plan->cfg=p->anchors[3];plan->map=p->map;plan->map_size=p->map_size;plan->digests=p->digests;plan->digest_size=p->digest_size;
    plan->index=p->index;plan->index_size=p->index_size;plan->manifest=p->manifest;memcpy(plan->index_sha,record+176,32);
    plan->cfg.manifest_count=cp_u32(record+268);plan->cfg.manifest_hash=p->manifest.checksum;
    uint64_t local=0,remote=0;unsigned param=0;
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,i,&row))goto done;
        if(!strcmp(row.path,"eboot.bin"))plan->cfg.eboot_size=row.size;
        if(!strcmp(row.path,"sce_sys/param.json")){if(row.flags!=BW_MANIFEST_LOCAL||!row.size||row.size>BW_SOURCE_PARAM_LIMIT)goto done;param++;}
        uint64_t *sum=row.flags==BW_MANIFEST_LOCAL?&local:&remote;if(row.size>UINT64_C(9007199254740991)-*sum)goto done;*sum+=row.size;
    }
    if(param!=1||plan->cfg.eboot_size<16||cp_validate_map(plan)||plan->local_count!=cp_u32(record+272)||
       local!=cp_u64(record+240)||remote!=cp_u64(record+248)||cp_index_rows_mode(plan,-1,0))goto done;
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,i,&row))goto done;
        if(na_required(&row)&&na_hash_required(p,i))goto done;}
    uint8_t marker[BW_GENERATION_SIZE];char marker_name[64],prepare_run[33];memcpy(prepare_run,record+16,32);prepare_run[32]=0;
    if(bw_generation_name(marker_name,prepare_run)||bw_generation_bytes(marker,prepare_run,identity->binding.title,identity->binding.game,record+112)||
       na_exact(p,3,marker_name,marker,sizeof marker))goto done;
    cp_sha_bytes(marker,sizeof marker,hash);if(memcmp(hash,record+208,32))goto done;
    ns_result(record,&identity->binding,0,&p->prepared);memcpy(p->generation,record+80,32);memcpy(p->map_sha256,record+112,32);
    struct bw_reader_selection *s=&p->selected;strcpy(s->source,identity->binding.source);strcpy(s->game,identity->binding.game);
    strcpy(s->title,identity->binding.title);strcpy(s->run,run);strcpy(s->binding_hex,identity->binding.game+10);
    for(unsigned i=0;i<32;i++)snprintf(s->prepared_record_hex+2*i,3,"%02x",p->prepared.record_sha256[i]);
    s->source_kind=BW_PROVIDER_FOLDER;s->source_policy=BW_PROVIDER_POLICY_V1;s->manifest_count=p->manifest.count;s->manifest_hash=p->manifest.checksum;s->eboot_size=plan->cfg.eboot_size;
    char serialized[BW_READER_SELECTION_BYTES+1];size_t serialized_size=0;
    if(bw_reader_selection_serialize(s,serialized,sizeof serialized,&serialized_size)||na_current(p))goto done;
    p->valid=1;*out=p;p=NULL;result=0;
done:
    if(error)*error=result?(p&&p->error?p->error:"Required startup files, manifest or receipt differ; choose Send to repair them."):NULL;
    free(plan);free(paths);free(record);free(json.bytes);memset(ns_games_path,0,sizeof ns_games_path);pthread_mutex_unlock(&ns_mutex);
    if(p)bw_native_launch_admission_close(&p);return result;
}
int bw_native_launch_admission_selection(struct bw_native_launch_admission *p,struct bw_reader_selection *out){
    if(out)memset(out,0,sizeof *out);if(!p||!p->valid||!out||na_current(p))return -1;*out=p->selected;return 0;
}
int bw_native_launch_admission_current(struct bw_native_launch_admission *p,const char *base,
    const struct bw_provider_binding *identity,const struct bw_reader_selection *selected,struct bw_native_prepared *out){
    if(out)memset(out,0,sizeof *out);
    if(!p||!p->valid||!base||strcmp(base,p->anchors[0].root)||!na_bound(&p->identity,identity)||na_selected(p,selected)||na_current(p))
        return na_fail(p,"The launch no longer matches its verified startup receipt.");
    if(out)*out=p->prepared;return 0;
}
int bw_native_launch_admission_source(struct bw_native_launch_admission *p,const struct bw_share_config *connection,
    struct bw_source_smb *source,const struct bw_reader_selection *selected,uint8_t map_sha[32],uint8_t generation[32],const char **error){
    const char *failure="The selected PC source or required startup metadata changed; choose Send to revalidate it.";
    if(error)*error=failure;struct bw_provider_binding identity;int result=-1;void *param=NULL;size_t size=0;
    if(!p||!connection||!source||!source->smb||!map_sha||!generation||source->error||
       bw_provider_binding_init(&identity,connection,p->identity.binding.source,p->identity.binding.title,BW_PROVIDER_FOLDER)||
       !na_bound(&p->identity,&identity)||na_selected(p,selected)||na_current(p))goto done;
    for(unsigned i=0;i<p->file_count;i++){
        struct na_file *file=&p->files[i];if(file->row==UINT32_MAX)continue;struct bw_manifest_record row;struct bw_source_info info;
        char path[2*BW_MANIFEST_MAX_PATH+2];if(bw_manifest_get(&p->manifest,file->row,&row))goto done;
        int n=snprintf(path,sizeof path,"%s/%s",p->identity.binding.source,row.path);if(n<0||(size_t)n>=sizeof path||na_guard(p))goto done;
        int rc;
        if(!strcmp(row.path,"sce_sys/param.json")){
            rc=bw_source_smb_small_file(source,path,BW_SOURCE_PARAM_LIMIT,&param,&size,&info);
            if(rc!=BW_SOURCE_FOUND||size!=row.size)goto done;
            char title[10];uint8_t hash[32];cp_sha_bytes(param,size,hash);
            if(bw_source_param_title(param,size,title)||strcmp(title,p->identity.binding.title)||memcmp(hash,p->digests+56u+32u*file->row,32))goto done;
            free(param);param=NULL;
        }else rc=bw_source_smb_file_info(source,path,&info);
        if(rc!=BW_SOURCE_FOUND||info.size!=row.size||bw_source_unix_mtime(info.mtime_ticks)!=row.mtime_sec||na_guard(p))goto done;
    }
    if(na_current(p))goto done;
    memcpy(map_sha,p->map_sha256,32);memcpy(generation,p->generation,32);p->source_checked=1;result=0;
done:
    free(param);if(result)na_fail(p,failure);if(error)*error=result?failure:NULL;return result;
}

int bw_native_launch_admission_map_sha256(struct bw_native_launch_admission *p,uint8_t out[32]){
    if(out)memset(out,0,32);if(!p||!p->valid||!out||na_current(p))return -1;memcpy(out,p->map_sha256,32);return 0;
}
