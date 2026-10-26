/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual folder whole-file/range reader over a modeled raw SMB PDU transport. */
#define _POSIX_C_SOURCE 200809L
#include "library/source_smb.h"
#include <time.h>
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"folder batching line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
enum {CREATE,INFO,CLOSE,READ};
struct smb2_pdu {smb2_command_cb cb;void *opaque;unsigned kind,id;uint64_t at;uint32_t size;uint8_t *bytes;};
struct smb2_context {const uint8_t *bytes;size_t size;uint32_t maximum,short_reply,largest;unsigned reads,opened,destroyed,action;int canceled,changed;struct smb2_pdu *pending;};
static const uint64_t ticks=UINT64_C(116444736010000000);
static struct smb2_pdu *operation(unsigned kind,unsigned id,smb2_command_cb cb,void *opaque){struct smb2_pdu *p=calloc(1,sizeof *p);CHECK(p);p->kind=kind;p->id=id;p->cb=cb;p->opaque=opaque;return p;}
struct smb2_pdu *smb2_cmd_create_async(struct smb2_context *s,struct smb2_create_request *r,smb2_command_cb cb,void *opaque){
    (void)s;unsigned id=!strcmp(r->name,"")?1:!strcmp(r->name,"fixture")?2:!strcmp(r->name,"fixture/eboot.bin")?3:0;CHECK(id);return operation(CREATE,id,cb,opaque);
}
struct smb2_pdu *smb2_cmd_query_info_async(struct smb2_context *s,struct smb2_query_info_request *r,smb2_command_cb cb,void *opaque){(void)s;CHECK(r->file_id[0]>=1&&r->file_id[0]<=3);return operation(INFO,r->file_id[0],cb,opaque);}
struct smb2_pdu *smb2_cmd_close_async(struct smb2_context *s,struct smb2_close_request *r,smb2_command_cb cb,void *opaque){(void)s;return operation(CLOSE,r->file_id[0],cb,opaque);}
struct smb2_pdu *smb2_cmd_read_async(struct smb2_context *s,struct smb2_read_request *r,smb2_command_cb cb,void *opaque){
    CHECK(r->file_id[0]==3&&r->length&&r->length<=s->maximum&&r->length<=1024u*1024u);
    if((s->action==13||s->action==14)&&r->length>65536)r->length=65536; /* SDK non-multi-credit clipping. */
    struct smb2_pdu *p=operation(READ,3,cb,opaque);
    p->at=r->offset;p->size=r->length;p->bytes=r->buf;s->reads++;if(p->size>s->largest)s->largest=p->size;return p;
}
void smb2_queue_pdu(struct smb2_context *s,struct smb2_pdu *p){
    if(p->kind==CREATE){struct smb2_create_reply r={0};r.file_id[0]=(uint8_t)p->id;r.file_attributes=p->id==3?0:0x10;r.end_of_file=p->id==3?s->size:0;r.last_write_time=r.change_time=ticks;s->opened++;p->cb(s,0,&r,p->opaque);}
    else if(p->kind==INFO){struct smb2_query_info_reply r={0};struct smb2_file_all_info *v=calloc(1,sizeof *v);CHECK(v);v->index_number=p->id;v->basic.file_attributes=p->id==3?0:0x10;v->standard.end_of_file=p->id==3?s->size:0;
        v->basic.last_write_time.tv_sec=v->basic.change_time.tv_sec=1;if(s->changed&&((s->action==8&&p->id==2)||(s->action!=8&&p->id==3)))v->basic.change_time.tv_usec=1;r.output_buffer=v;p->cb(s,0,&r,p->opaque);}
    else if(p->kind==CLOSE){CHECK(s->opened);s->opened--;p->cb(s,0,NULL,p->opaque);}
    else{
        if(s->action==5){CHECK(!s->pending);s->pending=p;return;}
        struct smb2_read_reply r={0};uint32_t size=p->size;if(s->short_reply&&size>s->short_reply)size=s->short_reply;
        CHECK(p->at<=s->size&&size<=s->size-p->at);
        if(s->action==6)size=0;else if(s->action!=7)memcpy(p->bytes,s->bytes+p->at,size);
        r.data_length=s->action==7?p->size+1:size;
        if(s->action==14&&s->reads==1){r.data_length=p->size+1;CHECK(p->at+r.data_length<=s->size);memcpy(p->bytes,s->bytes+p->at,r.data_length);}
        p->cb(s,0,&r,p->opaque);
    }
    free(p);
}
void smb2_free_pdu(struct smb2_context *s,struct smb2_pdu *p){(void)s;free(p);}
void smb2_free_data(struct smb2_context *s,void *p){(void)s;free(p);}
void smb2_destroy_context(struct smb2_context *s){s->destroyed++;s->opened=0;if(s->pending){struct smb2_pdu *p=s->pending;s->pending=NULL;p->bytes[0]=0xee;p->cb(s,-EIO,NULL,p->opaque);free(p);}}
t_socket smb2_get_fd(struct smb2_context *s){CHECK(s->pending);return -1;}
int smb2_which_events(struct smb2_context *s){(void)s;return 0;}
int smb2_service(struct smb2_context *s,int events){(void)s;(void)events;CHECK(0);return -1;}
uint32_t smb2_get_max_read_size(struct smb2_context *s){return s->maximum;}
struct sink {struct smb2_context *context;struct bw_source_smb *source;uint64_t at;size_t size,used,largest;unsigned calls;};
static int consume(void *opaque,const void *bytes,size_t size){struct sink *p=opaque;CHECK(size&&size<=65536&&size<=p->size-p->used);CHECK(!memcmp(bytes,p->context->bytes+p->at+p->used,size));p->used+=size;p->calls++;if(size>p->largest)p->largest=size;
    if(p->context->action==1)p->context->canceled=1;if(p->context->action==2||p->context->action==8)p->context->changed=1;if(p->context->action==3)return -1;if(p->context->action==4)p->source->deadline=0;return 0;}
static int current(void *opaque){return ((struct smb2_context *)opaque)->canceled;}
static double now(void){struct timespec t;CHECK(!clock_gettime(CLOCK_MONOTONIC,&t));return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
int main(void){
    const size_t size=2u*1024u*1024u+123;uint8_t *bytes=malloc(size);CHECK(bytes);for(size_t i=0;i<size;i++)bytes[i]=(uint8_t)(i*29u+i/251u);
    struct bw_source_snapshot snapshot;CHECK(!bw_source_snapshot_init_bound(&snapshot,"fixture","fixture"));snapshot.parent_count=1;snapshot.parents[0]=(struct bw_source_info){0,1,ticks,ticks,0x10};
    struct bw_source_info root={0,2,ticks,ticks,0x10},file={size,3,ticks,ticks,0};CHECK(!bw_source_snapshot_add(&snapshot,"",&root));CHECK(!bw_source_snapshot_add(&snapshot,"eboot.bin",&file));CHECK(!bw_source_snapshot_finish(&snapshot));
    unsigned cases=0,full_reads=0,small_reads=0;
    for(unsigned mode=0;mode<2;mode++)for(unsigned action=0;action<=14;action++){
        struct smb2_context context={.bytes=bytes,.size=size,.maximum=1024u*1024u,.action=action};
        if(action==9)context.maximum=262144;if(action==10)context.maximum=65536;if(action==11)context.short_reply=8191;if(action==12)context.maximum=0;
        struct bw_source_smb source={.smb=&context,.deadline=now()+30};uint64_t at=mode?17:0;size_t wanted=mode?size-31:size;struct sink sink={&context,&source,at,wanted,0,0,0};
        int result=mode?bw_source_smb_read_range(&source,&snapshot,"eboot.bin",at,wanted,current,&context,consume,&sink):bw_source_smb_read(&source,&snapshot,"eboot.bin",consume,&sink);
        int success=action==0||action==9||action==10||action==11||action==13||(!mode&&action==1);
        if(success){CHECK(!result&&sink.used==wanted&&source.content_bytes==wanted);uint32_t piece=action==13?65536:context.short_reply?context.short_reply:context.maximum;
            if(context.reads!=(wanted+piece-1)/piece)fprintf(stderr,"Payload reads: got %u expected %zu (bytes %zu, negotiated max %u)\n",context.reads,(wanted+piece-1)/piece,wanted,context.maximum);
            CHECK(context.reads==(wanted+piece-1)/piece);CHECK(sink.largest<=65536);if(action==13)CHECK(context.largest==65536);
            if(!mode&&action==0){full_reads=context.reads;CHECK(full_reads==3&&sink.calls==33);}if(!mode&&action==10){small_reads=context.reads;CHECK(small_reads==33);}}
        else{CHECK(result<0&&source.error);if(action==1||action==3||action==4)CHECK(sink.calls==1);if(action==5)CHECK(context.destroyed==1&&!context.pending&&!source.smb);if(action==6||action==7||action==12||action==14)CHECK(!sink.calls);if(action==14)CHECK(context.destroyed==1&&!source.smb);}
        CHECK(!context.opened);cases++;
    }
    struct smb2_context context={.bytes=bytes,.size=size,.maximum=1024u*1024u};struct bw_source_smb source={.smb=&context,.deadline=now()+30};struct sink sink={&context,&source,size,0,0,0,0};
    CHECK(!bw_source_smb_read_range(&source,&snapshot,"eboot.bin",size,0,current,&context,consume,&sink));CHECK(!context.reads&&!sink.calls&&!context.opened);
    printf("{\"passed\":true,\"checks\":%u,\"cases\":%u,\"wholeFileBytes\":%zu,\"batchedReadCalls\":%u,\"sixtyFourKiBReadCalls\":%u,\"maxConsumerBytes\":65536,\"modeledTransportOnly\":true}\n",checks,cases+1,size,full_reads,small_reads);
    bw_source_snapshot_destroy(&snapshot);free(bytes);return 0;
}
