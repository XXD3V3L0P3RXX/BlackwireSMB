#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "source_provider.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include "../streamreader/preparation_receipt.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static int failure(struct bw_source_provider *p,int error) {
    p->failed=1;if (!p->error) p->error=error;return -1;
}
static double monotonic(void) {
    struct timespec at;if (clock_gettime(CLOCK_MONOTONIC,&at)) return -1;
    return (double)at.tv_sec+(double)at.tv_nsec/1e9;
}
static int guard(void *opaque) {
    struct bw_source_provider *p=opaque;double at=monotonic();
    if (!p || p->failed || !p->source || !p->current || at<0 || at>=p->source->deadline || p->current(p->opaque))
        return p?failure(p,BW_COMPRESSED_DATA_CANCELLED):-1;
    return 0;
}
static int image_read(void *opaque,uint64_t at,void *bytes,size_t size) {
    struct bw_source_provider *p=opaque;
    if (guard(p)) return -1;
    int result=p->image_ops.pread(p->image_source,at,bytes,size);
    return result||guard(p)?-1:0;
}
static int image_current(void *opaque) {
    struct bw_source_provider *p=opaque;
    if (guard(p)) return -1;
    int result=p->image_ops.generation(p->image_source);
    return result||guard(p)?-1:0;
}
static void put64(uint8_t *out,uint64_t n) {for (unsigned i=0;i<8;i++) out[i]=(uint8_t)(n>>(8*i));}
static void image_generation(const struct bw_source_provider *p,const struct smb2_stat_64 *stat,uint8_t out[32]) {
    static const char domain[]="Blackwire immutable SMB image generation v1";
    uint8_t row[64];const uint64_t fields[]={stat->smb2_type,stat->smb2_ino,stat->smb2_size,
        stat->smb2_mtime,stat->smb2_mtime_nsec,stat->smb2_ctime,stat->smb2_ctime_nsec,p->identity.policy};
    for (unsigned i=0;i<8;i++) put64(row+8*i,fields[i]);
    struct cp_sha sha;cp_sha_init(&sha);cp_sha_update(&sha,domain,sizeof domain);
    cp_sha_update(&sha,p->identity.binding.sha256,32);cp_sha_update(&sha,row,sizeof row);
    cp_sha_update(&sha,p->container.map_sha256,32);cp_sha_final(&sha,out);
}
struct observed {struct bw_source_provider *provider;bw_source_observer_fn prior;void *opaque;};
static void observe(void *opaque,const struct bw_source_progress *progress) {
    struct observed *watch=opaque;struct bw_source_provider *p=watch->provider;
    if (guard(p) || (progress->phase==BW_SOURCE_HASHING && progress->bytes_total>p->budget)) {
        failure(p,progress->bytes_total>p->budget?BW_COMPRESSED_DATA_BUDGET:BW_COMPRESSED_DATA_CANCELLED);
        /* Existing source_prepare_bound is unchanged. Its next bounded SMB
         * request observes this owned deadline and safely closes/poisons as
         * required by the existing transport lifetime contract. */
        p->source->deadline=0;
    }
    if (watch->prior) watch->prior(watch->opaque,progress);
}
static int view_current(struct bw_source_provider *p) {
    if (guard(p)) return -1;
    int result;
    if (p->identity.kind==BW_PROVIDER_FOLDER) {
        struct observed watch={p,p->source->observe,p->source->observe_opaque};
        p->source->observe=observe;p->source->observe_opaque=&watch;
        result=bw_source_prepare_current(p->source,&p->folder);
        p->source->observe=watch.prior;p->source->observe_opaque=watch.opaque;
        if (!result && memcmp(p->bundle.generation_sha256,p->folder.snapshot.sha256,32)) result=-1;
    } else {
        result=bw_compressed_data_current(&p->container);
        if (!result) {
            uint8_t generation[32];image_generation(p,&p->container.image->admitted,generation);
            if (memcmp(p->bundle.generation_sha256,generation,32)) result=-1;
        }
    }
    return result||guard(p)?failure(p,BW_CONTAINER_STALE):0;
}
int bw_source_provider_current(struct bw_source_provider *p) {
    if (!p || !p->bundle.ready || p->busy) return p?failure(p,BW_COMPRESSED_DATA_STATE):-1;
    p->busy=1;int result=view_current(p);p->busy=0;return result;
}
int bw_source_provider_prepare(struct bw_source_provider *p,const struct bw_provider_binding *identity,
    struct bw_source_smb *source,struct bw_container_smb *image,const char *run,uint64_t budget,
    int (*current)(void *),void *opaque) {
    if (!p || p->source || p->busy || p->failed || !identity || !source || !current || !run || bw_generation_run(run) ||
        identity->policy!=BW_PROVIDER_POLICY_V1 || !budget || budget>BW_PROVIDER_MAX_LOCAL_BYTES ||
        (identity->kind!=BW_PROVIDER_FOLDER && identity->kind!=BW_PROVIDER_CONTAINER)) return -1;
    p->identity=*identity;p->source=source;p->budget=budget;p->current=current;p->opaque=opaque;p->busy=1;
    int result=guard(p);struct bw_provider_bundle *view=&p->bundle;
    if (result) goto done;
    const struct bw_source_binding *binding=&identity->binding;
    if (identity->kind==BW_PROVIDER_FOLDER) {
        struct observed watch={p,source->observe,source->observe_opaque};source->observe=observe;source->observe_opaque=&watch;
        result=bw_source_prepare_bound(source,binding->source,binding->game,binding->title,run,&p->folder);
        source->observe=watch.prior;source->observe_opaque=watch.opaque;
        if (result || p->failed) goto done;
        view->map=p->folder.map;view->digests=p->folder.digests;view->digest_size=p->folder.digest_size;
        memcpy(view->selection,p->folder.selection,sizeof view->selection);
        memcpy(view->generation_sha256,p->folder.snapshot.sha256,32);
    } else {
        if (!image) {result=-1;goto done;}
        p->borrowed_image=image;
        p->image_generation=image->admitted;
        p->image_ops=image->container.ops;p->image_source=image->container.source;
        image->container.ops=(struct bw_container_ops){image_read,image_current};image->container.source=p;
        result=bw_compressed_data_prepare(&p->container,image,binding,budget,source,guard,p);
        if (result) goto done;
        view->map=p->container.map;view->digests=p->container.digests;view->digest_size=p->container.digest_size;
        image_generation(p,&p->image_generation,view->generation_sha256);
        uint64_t boot=0;
        for (uint32_t i=0;i<p->container.manifest.count;i++) {
            struct bw_manifest_record row;
            if (bw_manifest_get(&p->container.manifest,i,&row)) {result=-1;goto done;}
            if (!strcmp(row.path,"eboot.bin")) boot=row.size;
        }
        if (bw_generation_run(run)) {result=-1;goto done;}
        int n=snprintf(view->selection,sizeof view->selection,
            "version=2\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%" PRIu64 "\nmanifest_count=%u\nmanifest_hash=%016" PRIx64 "\n",
            run,binding->title,binding->game,boot,p->container.manifest.count,p->container.manifest.checksum);
        if (n<0 || (size_t)n>=sizeof view->selection) {result=-1;goto done;}
    }
    memcpy(view->source,binding->source,sizeof view->source);memcpy(view->game,binding->game,sizeof view->game);
    memcpy(view->title,binding->title,sizeof view->title);view->ready=1;
    result=view_current(p);
done:
    p->busy=0;
    if (result || p->failed) {view->ready=0;return failure(p,p->error?p->error:BW_COMPRESSED_DATA_STATE);}
    bw_source_smb_progress(source,BW_SOURCE_VERIFIED,view->map.local_count,view->map.local_count,view->map.local_bytes,view->map.local_bytes);
    if (guard(p)) {view->ready=0;return -1;}
    return 0;
}
struct consuming {struct bw_source_provider *provider;bw_source_consume_fn consume;void *opaque;};
static int consume(void *opaque,const void *bytes,size_t size) {
    struct consuming *p=opaque;
    return size>65536 || guard(p->provider) || p->consume(p->opaque,bytes,size) || guard(p->provider)?-1:0;
}
int bw_source_provider_read(struct bw_source_provider *p,const char *path,bw_source_consume_fn consumer,void *opaque) {
    if (!p || !p->bundle.ready || p->busy || !path || !consumer) return -1;
    if (guard(p)) return -1;
    p->busy=1;int result=-1;struct consuming consumed={p,consumer,opaque};
    struct bw_manifest manifest;uint32_t id=UINT32_MAX;
    if (bw_manifest_open(&manifest,p->bundle.map.bytes,p->bundle.map.size)) goto done;
    for (uint32_t i=0;i<manifest.count;i++) {
        struct bw_manifest_record row;if (bw_manifest_get(&manifest,i,&row)) goto done;
        if (!strcmp(row.path,path) && row.flags==BW_MANIFEST_LOCAL) {id=i;break;}
    }
    if (id==UINT32_MAX) goto done;
    if (p->identity.kind==BW_PROVIDER_FOLDER)
        result=bw_source_smb_read(p->source,&p->folder.snapshot,path,consume,&consumed);
    else result=bw_compressed_data_copy_local(&p->container,id,consume,&consumed);
    if (!result) result=guard(p);
done:
    p->busy=0;return result?failure(p,BW_COMPRESSED_DATA_INTEGRITY):0;
}
void bw_source_provider_destroy(struct bw_source_provider *p) {
    if (!p) return;
    if (p->busy) {failure(p,BW_COMPRESSED_DATA_STATE);return;}
    if (p->borrowed_image && p->borrowed_image->container.source==p &&
        p->borrowed_image->container.ops.pread==image_read) {
        p->borrowed_image->container.ops=p->image_ops;p->borrowed_image->container.source=p->image_source;
    }
    bw_source_prepare_destroy(&p->folder);bw_compressed_data_destroy(&p->container);memset(p,0,sizeof *p);
}
