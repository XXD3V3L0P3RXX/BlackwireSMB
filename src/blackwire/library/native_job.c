#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "native_job.h"
#include "send_diagnostic.h"
#include <float.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct bw_native_jobs {
    pthread_mutex_t lock;
    struct bw_native_job_record jobs[BW_NATIVE_JOB_LIMIT];
    size_t count,next;
    uint64_t generation;
    int active;
};
static int nonce(const char *id){
    if(!id||strlen(id)!=32)return 0;
    for(unsigned i=0;i<32;i++)if(!((id[i]>='0'&&id[i]<='9')||(id[i]>='a'&&id[i]<='f')))return 0;
    return 1;
}
static struct bw_native_job_record *owned(const struct bw_native_job_token *token){
    struct bw_native_jobs *p=token->owner;if(p->active<0)return NULL;struct bw_native_job_record *job=&p->jobs[p->active];
    return job->generation==token->generation&&!memcmp(job->id,token->id,sizeof job->id)?job:NULL;
}
static const char *failure(enum bw_native_job_failure error){
    switch(error){
        case BW_JOB_SOURCE_CHANGED:return "The PC source changed; check it before preparing again.";
        case BW_JOB_CONNECTION_LOST:return "The PC share connection failed; check the PC and sharing settings.";
        case BW_JOB_DESTINATION_CONFLICT:return "Existing console files differ; they were preserved. Inspect the conflict before retrying.";
        case BW_JOB_CANCELLED:return "Preparation was cancelled. Check its last phase before retrying.";
        default:return "Preparation failed. Inspect the operation before retrying.";
    }
}
static void message(struct bw_native_job_record *job){
    const struct bw_source_progress *p=&job->progress;const char *text="Preparing required files";
    switch(p->phase){
        case BW_SOURCE_SCANNING:case BW_SOURCE_RECHECKING:
            snprintf(job->message,sizeof job->message,"%s: %u entries checked.",p->phase==BW_SOURCE_SCANNING?"Scanning PC files":"Checking that PC files are unchanged",p->phase_entries);return;
        case BW_SOURCE_METADATA:text="Checking game metadata";break;
        case BW_SOURCE_HASHING:text="Hashing startup files";break;
        case BW_SOURCE_VERIFIED:text="Startup source verified";break;
        case BW_SOURCE_PREFLIGHT:text="Checking existing console files";break;
        case BW_SOURCE_COPYING:text="Copying and verifying startup files";break;
        case BW_SOURCE_SPARSE:text="Preparing streamed file entries";break;
        case BW_SOURCE_INDEXING:text="Building the game index";break;
        case BW_SOURCE_INDEX_VERIFY:text="Verifying the game index";break;
        case BW_SOURCE_MEASURING:text="Measuring console storage";break;
        case BW_SOURCE_PREPARED:text="Startup files prepared; completing the preparation job";break;
        case BW_SOURCE_ERROR:text="Preparation reported an error; checking its result";break;
        default:break;
    }
    if(p->bytes_total)snprintf(job->message,sizeof job->message,"%s: %u/%u files, %.1f/%.1f MiB.",text,p->files_done,p->files_total,(double)p->bytes_done/1048576,(double)p->bytes_total/1048576);
    else if(p->files_total)snprintf(job->message,sizeof job->message,"%s: %u/%u files.",text,p->files_done,p->files_total);
    else snprintf(job->message,sizeof job->message,"%s.",text);
}
struct bw_native_jobs *bw_native_jobs_create(void){
    struct bw_native_jobs *p=calloc(1,sizeof *p);if(!p)return NULL;
    if(pthread_mutex_init(&p->lock,NULL)){free(p);return NULL;}p->active=-1;return p;
}
int bw_native_jobs_destroy(struct bw_native_jobs *p){
    if(!p||pthread_mutex_lock(&p->lock))return -1;
    int busy=p->active>=0;pthread_mutex_unlock(&p->lock);if(busy||pthread_mutex_destroy(&p->lock))return -1;free(p);return 0;
}
int bw_native_job_begin(struct bw_native_jobs *p,const char *id,const char *source,struct bw_native_job_token *token){
    if(!p||!token||!nonce(id)||!source||!strlen(source)||bw_manifest_validate_path(source,strlen(source))||strlen(source)>BW_MANIFEST_MAX_PATH)return -1;
    if(pthread_mutex_lock(&p->lock))return -1;
    int result=1;if(p->active>=0||p->generation==UINT64_MAX)goto done;
    for(size_t i=0;i<p->count;i++)if(!strcmp(p->jobs[i].id,id))goto done;
    size_t slot=p->next;struct bw_native_job_record *job=&p->jobs[slot];memset(job,0,sizeof *job);
    memcpy(job->id,id,33);memcpy(job->source,source,strlen(source)+1);job->generation=++p->generation;job->state=BW_JOB_QUEUED;
    snprintf(job->message,sizeof job->message,"Waiting to prepare required files.");
    p->active=(int)slot;p->next=(slot+1)%BW_NATIVE_JOB_LIMIT;if(p->count<BW_NATIVE_JOB_LIMIT)p->count++;
    bw_send_log("job-begin","id=%s source=%s",job->id,job->source);
    *token=(struct bw_native_job_token){.owner=p,.generation=job->generation};memcpy(token->id,id,33);result=0;
done:
    pthread_mutex_unlock(&p->lock);return result;
}
int bw_native_job_start(const struct bw_native_job_token *token){
    if(!token||!token->owner||pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job&&job->state==BW_JOB_QUEUED){job->state=BW_JOB_RUNNING;snprintf(job->message,sizeof job->message,"Starting required-file preparation.");result=0;}
    pthread_mutex_unlock(&token->owner->lock);return result;
}
int bw_native_job_update(const struct bw_native_job_token *token,const struct bw_source_progress *progress){
    if(!token||!token->owner||!progress||(unsigned)progress->phase>=BW_SOURCE_PHASE_COUNT||progress->files_done>progress->files_total||
        progress->bytes_done>progress->bytes_total||progress->phase_entries>progress->entries||
        !(progress->monotonic_seconds>=0&&progress->monotonic_seconds<=DBL_MAX))return -1;
    if(pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job&&job->state==BW_JOB_RUNNING&&progress->requests>=job->progress.requests&&progress->entries>=job->progress.entries&&
        progress->pages>=job->progress.pages&&progress->source_bytes>=job->progress.source_bytes&&progress->monotonic_seconds>=job->progress.monotonic_seconds){
        int log_progress=progress->phase!=job->progress.phase||progress->bytes_done/(16u*1048576u)!=job->progress.bytes_done/(16u*1048576u);
        job->progress=*progress;message(job);result=0;
        if(log_progress)bw_send_log("job-progress","id=%s phase=%u files=%u/%u bytes=%llu/%llu requests=%u entries=%u source_bytes=%llu message=%s",job->id,(unsigned)progress->phase,progress->files_done,progress->files_total,(unsigned long long)progress->bytes_done,(unsigned long long)progress->bytes_total,progress->requests,progress->entries,(unsigned long long)progress->source_bytes,job->message);
    }
    pthread_mutex_unlock(&token->owner->lock);return result;
}
void bw_native_job_observe(void *opaque,const struct bw_source_progress *progress){(void)bw_native_job_update(opaque,progress);}
int bw_native_job_setup_message(const struct bw_native_job_token *token,const char *text){
    if(!token||!token->owner||!text||strlen(text)>=384||pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job&&job->state==BW_JOB_RUNNING){snprintf(job->message,sizeof job->message,"%s",text);result=0;bw_send_log("job-setup-message","id=%s message=%s",job->id,text);}
    pthread_mutex_unlock(&token->owner->lock);return result;
}
int bw_native_job_reuse_prepared(const struct bw_native_job_token *token,uint32_t files,uint64_t bytes){
    if(!token||!token->owner||!files||!bytes||pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job&&job->state==BW_JOB_RUNNING){
        job->progress.phase=BW_SOURCE_PREPARED;job->progress.files_done=job->progress.files_total=files;
        job->progress.bytes_done=job->progress.bytes_total=bytes;job->files_prepared=1;
        snprintf(job->message,sizeof job->message,"Startup files kept. Finishing reader and PS5 dashboard setup.");result=0;
    }
    pthread_mutex_unlock(&token->owner->lock);return result;
}
int bw_native_job_finish_setup(const struct bw_native_job_token *token,int files_prepared,int registered,const char *text){
    if(!token||!token->owner||(files_prepared!=0&&files_prepared!=1)||(registered!=0&&registered!=1)||
       (registered&&!files_prepared)||!text||strlen(text)>=384||pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job&&job->state==BW_JOB_RUNNING){
        if(registered&&(job->progress.phase!=BW_SOURCE_PREPARED||!job->progress.files_total||
           job->progress.files_done!=job->progress.files_total||!job->progress.bytes_total||job->progress.bytes_done!=job->progress.bytes_total))goto done;
        job->files_prepared=(unsigned)files_prepared;job->registered=(unsigned)registered;
        job->failure=registered?BW_JOB_SUCCESS:BW_JOB_FAILED;job->state=registered?BW_JOB_DONE:BW_JOB_ERROR;
        snprintf(job->message,sizeof job->message,"%s",text);token->owner->active=-1;result=0;
        bw_send_log("job-setup-end","id=%s files_prepared=%d registered=%d message=%s",job->id,files_prepared,registered,text);
    }
done:
    pthread_mutex_unlock(&token->owner->lock);return result;
}
int bw_native_job_finish(const struct bw_native_job_token *token,enum bw_native_job_failure error){
    if(!token||!token->owner||(unsigned)error>BW_JOB_FAILED||pthread_mutex_lock(&token->owner->lock))return -1;
    struct bw_native_job_record *job=owned(token);int result=-1;
    if(job){
        if(error==BW_JOB_SUCCESS&&(job->state!=BW_JOB_RUNNING||job->progress.phase!=BW_SOURCE_PREPARED||!job->progress.files_total||
            job->progress.files_done!=job->progress.files_total||!job->progress.bytes_total||job->progress.bytes_done!=job->progress.bytes_total))goto done;
        bw_send_log("job-finish","id=%s result=%u",job->id,(unsigned)error);
        job->failure=error;job->state=error==BW_JOB_SUCCESS?BW_JOB_DONE:BW_JOB_ERROR;token->owner->active=-1;
        snprintf(job->message,sizeof job->message,"%s",error==BW_JOB_SUCCESS?"Startup files prepared. Registration and playback checks remain.":failure(error));result=0;
    }
done:
    pthread_mutex_unlock(&token->owner->lock);return result;
}
int bw_native_jobs_snapshot(struct bw_native_jobs *p,struct bw_native_job_view *out){
    if(!p||!out||pthread_mutex_lock(&p->lock))return -1;
    memset(out,0,sizeof *out);out->count=p->count;size_t oldest=p->count==BW_NATIVE_JOB_LIMIT?p->next:0;
    for(size_t i=0;i<p->count;i++)out->jobs[i]=p->jobs[(oldest+i)%BW_NATIVE_JOB_LIMIT];
    pthread_mutex_unlock(&p->lock);return 0;
}
struct json {char *bytes;size_t used,capacity;int bad;};
static void append(struct json *out,const char *format,...){
    if(out->bad)return;va_list ap;va_start(ap,format);int n=vsnprintf(out->bytes+out->used,out->capacity-out->used,format,ap);va_end(ap);
    if(n<0||(size_t)n>=out->capacity-out->used){out->bad=1;return;}out->used+=(size_t)n;
}
static void string(struct json *out,const char *text){
    append(out,"\"");for(const unsigned char *p=(const unsigned char *)text;*p;p++){
        if(*p=='"'||*p=='\\')append(out,"\\%c",*p);else if(*p<32)append(out,"\\u%04x",*p);else append(out,"%c",*p);
    }append(out,"\"");
}
int bw_native_jobs_json(struct bw_native_jobs *p,char **bytes,size_t *size){
    if(bytes)*bytes=NULL;if(size)*size=0;if(!p||!bytes||!size)return -1;
    struct bw_native_job_view *view=malloc(sizeof *view);if(!view)return -1;
    if(bw_native_jobs_snapshot(p,view)){free(view);return -1;}
    struct json out={.capacity=256u*1024u};out.bytes=malloc(out.capacity);if(!out.bytes){free(view);return -1;}
    static const char *states[]={"queued","running","done","error"};append(&out,"[");
    for(size_t i=0;i<view->count;i++){
        const struct bw_native_job_record *job=&view->jobs[i];const struct bw_source_progress *progress=&job->progress;
        append(&out,"%s{\"id\":",i?",":"");string(&out,job->id);append(&out,",\"action\":\"send\",\"name\":");string(&out,job->source);
        append(&out,",\"state\":");string(&out,states[job->state]);append(&out,",\"message\":");string(&out,job->message);
        if(job->state==BW_JOB_ERROR){append(&out,",\"error\":");string(&out,job->message);}
        append(&out,",\"filesPrepared\":%s,\"registered\":%s,\"canPlay\":false,\"progress\":{\"phase\":%u,\"files\":%u,\"filesTotal\":%u,\"bytes\":%llu,\"bytesTotal\":%llu,\"requests\":%u,\"entries\":%u,\"phaseEntries\":%u,\"sourceBytes\":%llu}}",
            job->files_prepared||job->state==BW_JOB_DONE?"true":"false",job->registered?"true":"false",(unsigned)progress->phase,progress->files_done,progress->files_total,
            (unsigned long long)progress->bytes_done,(unsigned long long)progress->bytes_total,progress->requests,progress->entries,progress->phase_entries,(unsigned long long)progress->source_bytes);
    }
    append(&out,"]");free(view);if(out.bad){free(out.bytes);return -1;}*bytes=out.bytes;*size=out.used;return 0;
}
