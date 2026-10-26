/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual compressed startup planner/hash/copy over a counted image transport. */
#define _POSIX_C_SOURCE 200809L
#include "library/compressed_data.h"
#include "library/source_smb.h"
#include "streamreader/copy_sha256.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,allocation_fail;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"compressed prepare line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
void *__real_malloc(size_t);
void *__wrap_malloc(size_t size){if(allocation_fail&&size==BW_COMPRESSED_DATA_READ_CHUNK){allocation_fail=0;return NULL;}return __real_malloc(size);}
struct fixture {uint8_t *image;size_t size;unsigned fences,reads;int canceled,changed,fail_read;struct bw_source_smb progress;};
struct sink {struct fixture *fixture;const uint8_t *original;size_t size,used,largest;unsigned calls,action;};
static uint8_t *load(const char *path,size_t *size){FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);*size=(size_t)n;CHECK(!fseek(f,0,SEEK_SET));uint8_t *bytes=malloc(*size);CHECK(bytes);CHECK(fread(bytes,1,*size,f)==*size);CHECK(!fclose(f));return bytes;}
static int image_read(void *opaque,uint64_t at,void *out,size_t size){struct fixture *p=opaque;p->reads++;if(p->fail_read){p->fail_read=0;return -1;}if(at>p->size||size>p->size-at)return -1;memcpy(out,p->image+at,size);return 0;}
static int generation(void *opaque){struct fixture *p=opaque;p->fences++;return p->changed?-1:0;}
static int current(void *opaque){return ((struct fixture *)opaque)->canceled;}
void bw_source_smb_progress(struct bw_source_smb *p,enum bw_source_phase phase,uint32_t files,uint32_t total,uint64_t bytes,uint64_t byte_total){p->progress.phase=phase;p->progress.files_done=files;p->progress.files_total=total;p->progress.bytes_done=bytes;p->progress.bytes_total=byte_total;}
static int consume(void *opaque,const void *bytes,size_t size){
    struct sink *p=opaque;CHECK(size&&size<=BW_COMPRESSED_DATA_CHUNK&&size<=p->size-p->used);CHECK(!memcmp(bytes,p->original+p->used,size));
    p->calls++;p->used+=size;if(size>p->largest)p->largest=size;
    if(p->action==1)p->fixture->canceled=1;
    if(p->action==2)p->fixture->changed=1;
    return p->action==3?-1:0;
}
static struct bw_container_smb *open_image(struct fixture *f){
    struct bw_container_smb *image=calloc(1,sizeof *image);CHECK(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;
    struct bw_container_ops ops={image_read,generation};CHECK(!bw_container_open(&image->container,&ops,f,f->size));return image;
}
static int prepare(struct fixture *f,struct bw_container_smb *image,struct bw_compressed_data *data){
    struct bw_source_binding binding={0};strcpy(binding.source,"fixture.ffpfsc");strcpy(binding.title,"PPSA99001");strcpy(binding.game,"PPSA99001_fixture");
    return bw_compressed_data_prepare(data,image,&binding,32u*1024u*1024u,&f->progress,current,f);
}
int main(int argc,char **argv){
    CHECK(argc==4);struct fixture fixture={0};fixture.image=load(argv[1],&fixture.size);size_t original_size,hash_size;uint8_t *original=load(argv[2],&original_size),*expected_hash=load(argv[3],&hash_size);
    CHECK(hash_size==32&&original_size>4u*1024u*1024u&&sizeof(struct bw_compressed_data)<65536);
    unsigned copy_fences=0,consumer_calls=0;
    for(unsigned action=0;action<6;action++){
        fixture.canceled=fixture.changed=fixture.fail_read=0;struct bw_container_smb *image=open_image(&fixture);struct bw_compressed_data data={0};
        CHECK(!prepare(&fixture,image,&data)&&data.ready&&data.buffer);uint32_t id=UINT32_MAX;
        for(uint32_t i=0;i<data.manifest.count;i++){struct bw_manifest_record row;CHECK(!bw_manifest_get(&data.manifest,i,&row));if(!strcmp(row.path,"eboot.bin")){id=i;CHECK(row.size==original_size);}}
        CHECK(id!=UINT32_MAX&&!memcmp(data.digests+56u+(size_t)id*32u,expected_hash,32));
        struct sink sink={&fixture,original,original_size,0,0,0,action};unsigned before=fixture.fences;
        if(action==4)data.digests[56u+(size_t)id*32u]^=1;
        if(action==5){image->container.pfsc.decoded_valid=0;image->container.pfsc.window_size=0;fixture.fail_read=1;}
        int result=bw_compressed_data_copy_local(&data,id,consume,&sink);
        if(!action){
            CHECK(!result&&sink.used==original_size&&sink.largest==BW_COMPRESSED_DATA_CHUNK);
            CHECK(sink.calls==(original_size+BW_COMPRESSED_DATA_CHUNK-1)/BW_COMPRESSED_DATA_CHUNK);
            size_t runs=(original_size+BW_COMPRESSED_DATA_READ_CHUNK-1)/BW_COMPRESSED_DATA_READ_CHUNK;
            copy_fences=fixture.fences-before;consumer_calls=sink.calls;CHECK(copy_fences<=3*runs+10);
        }else{
            CHECK(result<0&&data.failed);
            if(action==1||action==3)CHECK(sink.calls==1&&sink.used==BW_COMPRESSED_DATA_CHUNK);
            if(action==2)CHECK(sink.used==BW_COMPRESSED_DATA_READ_CHUNK&&sink.used<original_size);
            if(action>=4)CHECK(!sink.calls);
        }
        bw_compressed_data_destroy(&data);CHECK(!data.buffer&&!data.digests&&!data.files.files&&!data.map.bytes);free(image);
    }
    fixture.canceled=fixture.changed=fixture.fail_read=0;struct bw_container_smb *image=open_image(&fixture);struct bw_compressed_data data={0};allocation_fail=1;
    CHECK(prepare(&fixture,image,&data)==BW_PFS_MEMORY&&!allocation_fail&&!data.buffer&&!data.map.bytes&&!data.files.files);bw_compressed_data_destroy(&data);free(image);
    printf("{\"passed\":true,\"checks\":%u,\"copyGenerationFences\":%u,\"consumerCalls\":%u,\"inputRunBytes\":%u,\"maxConsumerBytes\":%u,\"faultCases\":6,\"modeledTransportOnly\":true}\n",checks,copy_fences,consumer_calls,BW_COMPRESSED_DATA_READ_CHUNK,BW_COMPRESSED_DATA_CHUNK);
    free(expected_hash);free(original);free(fixture.image);return 0;
}
