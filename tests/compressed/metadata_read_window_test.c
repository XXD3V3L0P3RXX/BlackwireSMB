/* SPDX-License-Identifier: GPL-3.0-or-later */
#define _DEFAULT_SOURCE 1
#define main maintained_catalog_failure_main
#define smb2_pread maintained_smb2_pread
#define smb2_destroy_context maintained_smb2_destroy_context
#define smb2_get_max_read_size maintained_smb2_get_max_read_size
#include "../diagnostics/test_catalog_image_failure.c"
#undef main
#undef smb2_pread
#undef smb2_destroy_context
#undef smb2_get_max_read_size
#include "streamreader/metadata_read_window.h"
int smb2_pread(struct smb2_context *,struct smb2fh *,uint8_t *,uint32_t,uint64_t);
void smb2_destroy_context(struct smb2_context *);
uint32_t smb2_get_max_read_size(struct smb2_context *);
/* Include the actual adapter to exercise its callback/fence/lifetime boundary,
 * not a second implementation of the cached transport. */
#include "streamreader/container_smb.c"

static unsigned cases,fetches,free_events,drain_events,sequence,cache_freed_at,drained_at;
static size_t largest_fetch;static int fail_fetch,fail_cache_allocation;
static uint32_t maximum_override,largest_request;
static void *tracked_cache,*pending_destination;
static struct bw_container_smb *observed_image;
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size){
    if(size==sizeof(struct bw_metadata_read_window)){
        if(fail_cache_allocation){errno=ENOMEM;return NULL;}
        void *p=__real_malloc(size);C(p&&!tracked_cache);tracked_cache=p;return p;
    }
    return __real_malloc(size);
}
void __real_free(void *);
void __wrap_free(void *p){
    if(p&&p==tracked_cache){C(!observed_image||!observed_image->metadata_window_active);cache_freed_at=++sequence;free_events++;tracked_cache=NULL;}
    __real_free(p);
}
void smb2_destroy_context(struct smb2_context *p){
    drained_at=++sequence;drain_events++;
    if(pending_destination){C(observed_image&&observed_image->metadata_window_active&&tracked_cache);((uint8_t *)pending_destination)[0]=0xee;pending_destination=NULL;}
    maintained_smb2_destroy_context(p);
}
int smb2_pread(struct smb2_context *s,struct smb2fh *h,uint8_t *b,uint32_t n,uint64_t at){
    if(maximum_override)C(n<=maximum_override);if(n>largest_request)largest_request=n;
    if(!strcmp(mode,"retained-error")){pending_destination=b;errno=EIO;return -EIO;}
    return maintained_smb2_pread(s,h,b,n,at);
}
uint32_t smb2_get_max_read_size(struct smb2_context *s){return maximum_override?maximum_override:maintained_smb2_get_max_read_size(s);}
static uint8_t pattern(uint64_t at){return (uint8_t)(at*29u+at/251u);}
static int fetch(void *opaque,uint64_t at,void *out,size_t n){
    (void)opaque;fetches++;if(n>largest_fetch)largest_fetch=n;
    for(size_t i=0;i<n;i++)((uint8_t *)out)[i]=pattern(at+i);
    return fail_fetch&&fetches==(unsigned)fail_fetch?-7:0;
}
static void equal_pattern(const void *out,uint64_t at,size_t n){for(size_t i=0;i<n;i++)C(((const uint8_t *)out)[i]==pattern(at+i));}
static void pure(void){
    struct bw_metadata_read_window *w=__real_malloc(sizeof *w);uint8_t *out=__real_malloc(BW_METADATA_WINDOW_MAX_READ+1);C(w&&out);
    const uint64_t image=12u*BW_METADATA_WINDOW_PAGE+19;bw_metadata_window_reset(w);fetches=0;
    cases++;C(!bw_metadata_window_read(w,image,0,out,1,fetch,NULL));equal_pattern(out,0,1);C(fetches==1);
    C(!bw_metadata_window_read(w,image,17,out,300,fetch,NULL));equal_pattern(out,17,300);C(fetches==1);
    cases++;C(!bw_metadata_window_read(w,image,BW_METADATA_WINDOW_PAGE-7,out,19,fetch,NULL));equal_pattern(out,BW_METADATA_WINDOW_PAGE-7,19);C(fetches==2);
    cases++;C(!bw_metadata_window_read(w,image,image-3,out,3,fetch,NULL));equal_pattern(out,image-3,3);C(fetches==3);
    unsigned before=fetches;C(!bw_metadata_window_read(w,image,image,NULL,0,fetch,NULL)&&fetches==before);
    cases++;C(bw_metadata_window_read(w,image,image,out,1,fetch,NULL)<0&&!w->valid&&fetches==before);
    C(bw_metadata_window_read(w,UINT64_MAX,UINT64_MAX-1,out,2,fetch,NULL)<0&&!w->valid);
    cases++;C(!bw_metadata_window_read(w,UINT64_MAX,UINT64_MAX-1,out,1,fetch,NULL));equal_pattern(out,UINT64_MAX-1,1);
    cases++;bw_metadata_window_reset(w);fetches=0;
    for(unsigned i=0;i<8;i++)C(!bw_metadata_window_read(w,image,i*BW_METADATA_WINDOW_PAGE,out,1,fetch,NULL));
    C(fetches==8);C(!bw_metadata_window_read(w,image,0,out,1,fetch,NULL)&&fetches==8);
    C(!bw_metadata_window_read(w,image,8u*BW_METADATA_WINDOW_PAGE,out,1,fetch,NULL)&&fetches==9);
    C(!bw_metadata_window_read(w,image,0,out,1,fetch,NULL)&&fetches==9);
    C(!bw_metadata_window_read(w,image,BW_METADATA_WINDOW_PAGE,out,1,fetch,NULL)&&fetches==10);
    cases++;bw_metadata_window_reset(w);fetches=0;fail_fetch=2;
    C(bw_metadata_window_read(w,image,BW_METADATA_WINDOW_PAGE-4,out,8,fetch,NULL)==-7&&!w->valid&&fetches==2);
    fail_fetch=0;C(!bw_metadata_window_read(w,image,BW_METADATA_WINDOW_PAGE-4,out,8,fetch,NULL)&&fetches==4);equal_pattern(out,BW_METADATA_WINDOW_PAGE-4,8);
    cases++;bw_metadata_window_reset(w);fetches=0;largest_fetch=0;
    C(!bw_metadata_window_read(w,2u*BW_METADATA_WINDOW_MAX_READ,17,out,65536,fetch,NULL));equal_pattern(out,17,65536);C(fetches==1&&largest_fetch==65536&&!w->valid);
    C(!bw_metadata_window_read(w,2u*BW_METADATA_WINDOW_MAX_READ,19,out,BW_METADATA_WINDOW_MAX_READ,fetch,NULL));equal_pattern(out,19,BW_METADATA_WINDOW_MAX_READ);C(fetches==2&&largest_fetch==BW_METADATA_WINDOW_MAX_READ);
    C(bw_metadata_window_read(w,2u*BW_METADATA_WINDOW_MAX_READ,0,out,BW_METADATA_WINDOW_MAX_READ+1u,fetch,NULL)<0&&fetches==2);
    cases++;C(!bw_metadata_window_read(NULL,image,13,out,31,fetch,NULL));equal_pattern(out,13,31);
    C(!bw_metadata_window_read(w,0,0,NULL,0,fetch,NULL));C(bw_metadata_window_read(w,0,1,NULL,0,fetch,NULL)<0);
    C(bw_metadata_window_read(w,image,0,out,1,NULL,NULL)<0&&!w->valid);
    cases++;bw_metadata_window_reset(w);uint32_t random=7;
    for(unsigned i=0;i<400;i++){random=random*1664525u+1013904223u;size_t n=random%65536u;uint64_t at=(random>>5)%(image-n+1);C(!bw_metadata_window_read(w,image,at,out,n,fetch,NULL));equal_pattern(out,at,n);if(i%17u==0)bw_metadata_window_reset(w);}
    __real_free(w);__real_free(out);
}
static void reset_counts(void){stats=heldstats=reads=opens=closes=destroys=0;cache_freed_at=drained_at=sequence=0;pending_destination=NULL;C(!tracked_cache);}
static void begin(struct bw_source_smb *s,struct bw_container_smb *image,const char *fault,int which){
    reset_counts();mode=fault;source=s;memset(s,0,sizeof *s);s->smb=calloc(1,sizeof *s->smb);C(s->smb);
    struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));s->deadline=t.tv_sec+t.tv_nsec/1e9+120;observed_image=image;
    int result=which==0?bw_container_smb_open(image,s->smb,"Neutral.ffpfsc"):which==1?bw_container_smb_open_owned(image,&s->smb,"Neutral.ffpfsc"):bw_container_smb_open_owned_metadata(image,&s->smb,"Neutral.ffpfsc");
    C(!result&&image->container.ready);C((image->metadata_window!=NULL)==(which==2&&!fail_cache_allocation));
}
static void end(struct bw_source_smb *s,struct bw_container_smb *image){
    C(!bw_container_smb_close(image)&&!image->metadata_window&&!image->metadata_window_active);
    if(s->smb){smb2_destroy_context(s->smb);s->smb=NULL;}C(destroys==1&&!tracked_cache&&!pending_destination);observed_image=NULL;
}
static void adapter(void){
    struct bw_source_smb s;struct bw_container_smb image;uint8_t out[65536];unsigned uncached_reads=0;
    for(int which=0;which<3;which++){
        cases++;begin(&s,&image,"valid",which);unsigned before=reads;
        C(!exact_read(&image,100,out,1));uint8_t first=out[0];C(!exact_read(&image,101,out,1));
        if(which==2){C(reads==before+1&&image.metadata_window->valid);C(!exact_read(&image,100,out,1)&&out[0]==first&&reads==before+1);}
        else C(reads==before+2);
        C(!generation(&image));C(!image.metadata_window||!image.metadata_window->valid);
        before=reads;C(!exact_read(&image,0,out,sizeof out));C(reads==before+1);
        if(!which)uncached_reads=reads;end(&s,&image);
    }
    cases++;fail_cache_allocation=1;begin(&s,&image,"valid",2);C(!image.metadata_window&&reads==uncached_reads-3);end(&s,&image);fail_cache_allocation=0;
    cases++;begin(&s,&image,"short-read",2);C(reads>1);end(&s,&image);
    cases++;maximum_override=4096;largest_request=0;begin(&s,&image,"valid",2);unsigned before=reads;
    C(!exact_read(&image,100,out,1)&&reads==before+4);before=reads;
    C(!exact_read(&image,0,out,sizeof out)&&reads==before+16&&largest_request<=4096);end(&s,&image);maximum_override=0;
    cases++;begin(&s,&image,"valid",2);mode="retained-error";C(exact_read(&image,100,out,1)<0);
    C(!s.smb&&!image.metadata_window&&image.first_failure_result==-EIO&&image.first_failure_result_known&&image.first_failure_operation==BW_IMAGE_FAILURE_PREAD);
    C(drained_at&&cache_freed_at>drained_at&&!pending_destination);C(!bw_container_smb_close(&image));observed_image=NULL;
    cases++;begin(&s,&image,"valid",2);C(!exact_read(&image,100,out,1));mode="generation-fstat";C(generation(&image)<0&&!s.smb&&!image.metadata_window);C(image.first_failure_operation==BW_IMAGE_FAILURE_FSTAT);C(drained_at&&cache_freed_at>drained_at);C(!bw_container_smb_close(&image));observed_image=NULL;
    cases++;begin(&s,&image,"valid",2);C(!exact_read(&image,100,out,1));mode="held-inode";C(generation(&image)<0&&s.smb&&!image.metadata_window);C(image.first_failure_operation==BW_IMAGE_FAILURE_GENERATION_METADATA);mode="valid";end(&s,&image);
    cases++;begin(&s,&image,"valid",2);C(!exact_read(&image,100,out,1));mode="pread-max-read";C(exact_read(&image,32768,out,1)<0&&s.smb&&!image.metadata_window);C(image.first_failure_operation==BW_IMAGE_FAILURE_MAX_READ&&!image.first_failure_result_known);mode="valid";end(&s,&image);
    cases++;begin(&s,&image,"valid",2);C(exact_read(&image,UINT64_MAX,out,1)<0&&!image.metadata_window);C(image.first_failure_operation==BW_IMAGE_FAILURE_READ_BOUNDS);end(&s,&image);
    cases++;begin(&s,&image,"valid",2);mode="pread-eof-close-denied";C(exact_read(&image,32768,out,1)<0&&s.smb&&!image.metadata_window);C(image.first_failure_result==0&&image.first_failure_result_known&&image.first_failure_operation==BW_IMAGE_FAILURE_PREAD);C(bw_container_smb_close(&image)==-EACCES&&!s.smb&&image.first_failure_result==0&&image.first_failure_operation==BW_IMAGE_FAILURE_PREAD);observed_image=NULL;
    cases++;begin(&s,&image,"valid",2);mode="close";C(bw_container_smb_close(&image)==-EIO&&!s.smb&&!image.metadata_window);C(drained_at&&cache_freed_at>drained_at);observed_image=NULL;
    cases++;struct smb2_context *absent=NULL;C(bw_container_smb_open_owned_metadata(&image,NULL,"Neutral.ffpfsc")==BW_PFS_ARGUMENT);C(!image.metadata_window);C(bw_container_smb_open_owned_metadata(&image,&absent,"Neutral.ffpfsc")==BW_PFS_ARGUMENT);C(!image.metadata_window);
}
int main(int argc,char **argv){C(argc==2);fixture=argv[1];pure();adapter();C(!tracked_cache&&!pending_destination);printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"cacheFreeEvents\":%u,\"contextDrainEvents\":%u,\"windowDataBytes\":%u,\"windowAllocationBytes\":%zu,\"syntheticOnly\":true,\"consoleActions\":0,\"networkActions\":0}\n",cases,checks,free_events,drain_events,BW_METADATA_WINDOW_PAGE*BW_METADATA_WINDOW_SLOTS,sizeof(struct bw_metadata_read_window));return 0;}
