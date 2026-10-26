/* Actual source preparation/provider, parser, raw SMB decoder and namespace
 * observer. Only the SMB PDU service and image bytes are local test adapters. */
#define _DEFAULT_SOURCE 1
#define _GNU_SOURCE 1
#include "library/source_namespace.h"
#include "streamreader/copy_sha256.h"
#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>
#include <smb2/smb2-errors.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"namespace line%u %s\n",__LINE__,#x);exit(2);}}while(0)
struct handle {int fd,listed;char path[1200];};
struct smb2_context {char root[1024];struct handle handles[64];unsigned open,requests,reads;int changed,canceled,destroyed;};
enum {CREATE,INFO,CLOSE,READ,PAGE};
struct smb2_pdu {smb2_command_cb cb;void *opaque;unsigned kind,id;char path[1024];uint64_t at;uint32_t size;uint8_t *bytes;};
static struct smb2_pdu *pdu(unsigned kind,unsigned id,smb2_command_cb cb,void *opaque){struct smb2_pdu *p=calloc(1,sizeof *p);C(p);p->kind=kind;p->id=id;p->cb=cb;p->opaque=opaque;return p;}
struct smb2_pdu *smb2_cmd_create_async(struct smb2_context *s,struct smb2_create_request *r,smb2_command_cb cb,void *opaque){
    (void)s;C(r->create_disposition==SMB2_FILE_OPEN&&(r->create_options&SMB2_FILE_OPEN_REPARSE_POINT)&&r->share_access==SMB2_FILE_SHARE_READ);
    struct smb2_pdu *p=pdu(CREATE,0,cb,opaque);C(strlen(r->name)<sizeof p->path);strcpy(p->path,r->name);return p;}
struct smb2_pdu *smb2_cmd_query_info_async(struct smb2_context *s,struct smb2_query_info_request *r,smb2_command_cb cb,void *opaque){(void)s;return pdu(INFO,r->file_id[0],cb,opaque);}
struct smb2_pdu *smb2_cmd_close_async(struct smb2_context *s,struct smb2_close_request *r,smb2_command_cb cb,void *opaque){(void)s;return pdu(CLOSE,r->file_id[0],cb,opaque);}
struct smb2_pdu *smb2_cmd_query_directory_async(struct smb2_context *s,struct smb2_query_directory_request *r,smb2_command_cb cb,void *opaque){(void)s;C(r->file_information_class==SMB2_FILE_ID_FULL_DIRECTORY_INFORMATION);return pdu(PAGE,r->file_id[0],cb,opaque);}
struct smb2_pdu *smb2_cmd_read_async(struct smb2_context *s,struct smb2_read_request *r,smb2_command_cb cb,void *opaque){(void)s;struct smb2_pdu *p=pdu(READ,r->file_id[0],cb,opaque);p->at=r->offset;p->size=r->length;p->bytes=r->buf;return p;}
static unsigned attributes(const struct stat *st){return S_ISDIR(st->st_mode)?0x10u:S_ISLNK(st->st_mode)?0x400u:S_ISREG(st->st_mode)?0u:0x40u;}
static uint64_t ticks(time_t sec,long nsec){return UINT64_C(116444736000000000)+(uint64_t)sec*10000000u+(uint64_t)nsec/100;}
static void put32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void put64(uint8_t *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(i*8));}
void smb2_queue_pdu(struct smb2_context *s,struct smb2_pdu *p){s->requests++;
    if(p->kind==CREATE){unsigned id=1;while(id<64&&s->handles[id].fd>=0)id++;C(id<64);struct handle *h=s->handles+id;
        C(snprintf(h->path,sizeof h->path,"%s%s%s",s->root,*p->path?"/":"",p->path)>0);struct stat st;C(!lstat(h->path,&st));
        h->fd=open(h->path,S_ISLNK(st.st_mode)?O_PATH|O_NOFOLLOW:O_RDONLY|O_NOFOLLOW);C(h->fd>=0);h->listed=0;s->open++;
        struct smb2_create_reply r={0};r.file_id[0]=(uint8_t)id;r.file_attributes=attributes(&st);r.end_of_file=S_ISREG(st.st_mode)?(uint64_t)st.st_size:0;
        r.last_write_time=ticks(st.st_mtim.tv_sec,st.st_mtim.tv_nsec);r.change_time=ticks(st.st_ctim.tv_sec+s->changed,st.st_ctim.tv_nsec);p->cb(s,0,&r,p->opaque);
    }else{C(p->id&&p->id<64&&s->handles[p->id].fd>=0);struct handle *h=s->handles+p->id;struct stat st;C(!fstat(h->fd,&st));
        if(p->kind==INFO){struct smb2_file_all_info *v=calloc(1,sizeof *v);C(v);v->index_number=(uint64_t)st.st_ino;v->basic.file_attributes=attributes(&st);v->standard.end_of_file=S_ISREG(st.st_mode)?(uint64_t)st.st_size:0;
            v->basic.last_write_time.tv_sec=st.st_mtim.tv_sec;v->basic.last_write_time.tv_usec=st.st_mtim.tv_nsec/1000;
            v->basic.change_time.tv_sec=st.st_ctim.tv_sec+s->changed;v->basic.change_time.tv_usec=st.st_ctim.tv_nsec/1000;struct smb2_query_info_reply r={0};r.output_buffer=v;p->cb(s,0,&r,p->opaque);
        }else if(p->kind==CLOSE){C(!close(h->fd)&&s->open);h->fd=-1;s->open--;p->cb(s,0,NULL,p->opaque);
        }else if(p->kind==READ){C(S_ISREG(st.st_mode));ssize_t n=pread(h->fd,p->bytes,p->size,(off_t)p->at);C(n>=0);struct smb2_read_reply r={0};r.data_length=(uint32_t)n;s->reads++;p->cb(s,0,&r,p->opaque);
        }else{C(p->kind==PAGE&&S_ISDIR(st.st_mode));if(h->listed)p->cb(s,SMB2_STATUS_NO_MORE_FILES,NULL,p->opaque);
            else{h->listed=1;uint8_t *wire=calloc(1,65536);C(wire);size_t used=0,previous=0;unsigned rows=0;DIR *dir=opendir(h->path);C(dir);struct dirent *entry;
                while((entry=readdir(dir))){if(!strcmp(entry->d_name,".")||!strcmp(entry->d_name,".."))continue;size_t n=strlen(entry->d_name);C(n&&n<256);char child[1500];C(snprintf(child,sizeof child,"%s/%s",h->path,entry->d_name)>0);struct stat childst;C(!lstat(child,&childst));
                    size_t span=(80+2*n+7)&~(size_t)7;C(used+span<65536);if(rows)put32(wire+previous,(uint32_t)(used-previous));previous=used;uint8_t *row=wire+used;
                    put64(row+24,ticks(childst.st_mtim.tv_sec,childst.st_mtim.tv_nsec));put64(row+32,ticks(childst.st_ctim.tv_sec+s->changed,childst.st_ctim.tv_nsec));put64(row+40,S_ISREG(childst.st_mode)?(uint64_t)childst.st_size:0);
                    put32(row+56,attributes(&childst));put32(row+60,(uint32_t)(2*n));put64(row+72,(uint64_t)childst.st_ino);for(size_t j=0;j<n;j++){C((uint8_t)entry->d_name[j]<128);row[80+2*j]=(uint8_t)entry->d_name[j];}used+=span;rows++;
                }C(!closedir(dir));if(!rows)p->cb(s,SMB2_STATUS_NO_MORE_FILES,NULL,p->opaque);else{struct smb2_query_directory_reply r={0};r.output_buffer=wire;r.output_buffer_length=(uint32_t)used;p->cb(s,0,&r,p->opaque);}free(wire);
            }
        }
    }free(p);
}
void smb2_free_pdu(struct smb2_context *s,struct smb2_pdu *p){(void)s;free(p);}
void smb2_free_data(struct smb2_context *s,void *p){(void)s;free(p);}
void smb2_destroy_context(struct smb2_context *s){for(unsigned i=1;i<64;i++)if(s->handles[i].fd>=0){C(!close(s->handles[i].fd));s->handles[i].fd=-1;}s->open=0;s->destroyed=1;}
t_socket smb2_get_fd(struct smb2_context *s){(void)s;C(0);return -1;}
int smb2_which_events(struct smb2_context *s){(void)s;return 0;}
int smb2_service(struct smb2_context *s,int events){(void)s;(void)events;C(0);return -1;}
uint32_t smb2_get_max_read_size(struct smb2_context *s){(void)s;return 65536;}
static double now(void){struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec/1e9;}
static int current(void *p){return ((struct smb2_context *)p)->canceled;}
struct image_source {uint8_t *bytes;size_t size;unsigned fences;struct smb2_context *context;};
static int image_read(void *p,uint64_t at,void *out,size_t size){struct image_source *i=p;if(at>i->size||size>i->size-at)return -1;memcpy(out,i->bytes+at,size);return 0;}
static int image_generation(void *p){struct image_source *i=p;i->fences++;return i->context->changed?-1:0;}
static void load_image(struct image_source *image,const char *path){FILE *f=fopen(path,"rb");C(f&&!fseek(f,0,SEEK_END));long n=ftell(f);C(n>0&&!fseek(f,0,SEEK_SET));image->size=(size_t)n;image->bytes=malloc(image->size);C(image->bytes&&fread(image->bytes,1,image->size,f)==image->size&&!fclose(f));}
static int hash_copy(void *opaque,const void *bytes,size_t size){C(size<=65536);cp_sha_update(opaque,bytes,size);return 0;}
int main(int argc,char **argv){C(argc==7);int folder=!strcmp(argv[1],"folder"),valid=atoi(argv[6]);unsigned k1=(unsigned)atoi(argv[4]),k2=(unsigned)atoi(argv[5]);struct smb2_context context={0};
    C(strlen(argv[2])<sizeof context.root);strcpy(context.root,argv[2]);for(unsigned i=0;i<64;i++)context.handles[i].fd=-1;
    struct bw_source_smb source={.smb=&context,.deadline=now()+600};struct bw_share_config config={0};strcpy(config.server,"192.0.2.1");strcpy(config.share,"FixtureShare");
    struct bw_provider_binding binding;C(!bw_provider_binding_init(&binding,&config,folder?"fixture":"fixture.ffpfsc","PPSA99001",folder?BW_PROVIDER_FOLDER:BW_PROVIDER_CONTAINER));
    struct bw_container_smb *image=NULL;struct image_source input={.context=&context};
    if(!folder){load_image(&input,argv[3]);image=calloc(1,sizeof *image);C(image);strcpy(image->path,"fixture.ffpfsc");image->handle=(struct smb2fh *)image;
        image->admitted.smb2_ino=7;image->admitted.smb2_size=input.size;image->admitted.smb2_mtime=1700000000;
        struct bw_container_ops ops={image_read,image_generation};int opened=bw_container_open(&image->container,&ops,&input,input.size);
        if(opened){C(!valid);printf("{\"passed\":true,\"checks\":%u,\"refusedDuringParserOpen\":true}\n",checks);free(image);free(input.bytes);return 0;}
        struct bw_source_namespace_view observed;int observed_result=bw_source_namespace_container(&image->container,&observed);
        if(valid||k1||k2)C(!observed_result&&observed.kind[0]==k1&&observed.kind[1]==k2);
        else C(observed_result<0);
    }
    struct bw_source_provider provider={0};int result=bw_source_provider_prepare_policy(&provider,&binding,&source,image,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",32u*1024u*1024u,current,&context,BW_SOURCE_PLAN_COMPANIONS_V1);
    if(!valid){C(result<0&&!provider.bundle.ready);C(!source.content_bytes&&!provider.container.hashed_bytes);}
    else{C(!result&&provider.bundle.ready&&provider.bundle.plan_policy==BW_SOURCE_PLAN_COMPANIONS_V1);struct bw_source_companion_namespace *ns=NULL;const char *error=NULL;
        C(!bw_source_provider_companion_namespace(&provider,&ns,&error)&&ns&&!error);struct bw_source_namespace_view view;C(!bw_source_companion_namespace_matches(ns,&provider));struct bw_source_provider other={0};C(bw_source_companion_namespace_matches(ns,&other)<0);C(!bw_source_companion_namespace_view(ns,&view));
        unsigned flags=(k1==1||k1==2?1u:0u)|(k2==1||k2==2?2u:0u);C(view.kind[0]==k1&&view.kind[1]==k2&&view.presence_flags==flags&&view.selected==(flags&2u?2u:flags&1u?1u:0u));
        struct bw_manifest map;C(!bw_manifest_open(&map,provider.bundle.map.bytes,provider.bundle.map.size));unsigned locals=0;for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&map,i,&row));
            if(bw_source_companion_path_namespace(row.path)>0){C(row.flags==BW_MANIFEST_LOCAL);uint8_t zero[32]={0},copy[32];C(memcmp(provider.bundle.digests+56u+32u*i,zero,32));struct cp_sha hash;cp_sha_init(&hash);C(!bw_source_provider_read(&provider,row.path,hash_copy,&hash));cp_sha_final(&hash,copy);C(!memcmp(copy,provider.bundle.digests+56u+32u*i,32));locals++;}}
        if(k1==2||k2==2)C(locals>0);
        context.changed=1;C(bw_source_companion_namespace_current(ns)<0);C(bw_source_companion_namespace_view(ns,&view)<0);bw_source_companion_namespace_close(&ns);C(!ns);
    }
    bw_source_provider_destroy(&provider);C(!context.open);
    if(valid){context.changed=0;memset(&source,0,sizeof source);source.smb=&context;source.deadline=now()+600;
        if(image){struct bw_container_ops ops={image_read,image_generation};C(!bw_container_open(&image->container,&ops,&input,input.size));}
        struct bw_source_provider legacy={0};C(!bw_source_provider_prepare(&legacy,&binding,&source,image,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",32u*1024u*1024u,current,&context));C(legacy.bundle.plan_policy==BW_SOURCE_PLAN_LEGACY);
        struct bw_manifest map;C(!bw_manifest_open(&map,legacy.bundle.map.bytes,legacy.bundle.map.size));for(uint32_t i=0;i<map.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&map,i,&row));if(row.size&&bw_source_companion_path_namespace(row.path)>0){
            const char *leaf=strrchr(row.path,'/');leaf=leaf?leaf+1:row.path;size_t n=strlen(leaf);
            if(n>=5&&!strcmp(leaf+n-5,".sprx"))C(row.flags==BW_MANIFEST_LOCAL);
            else if(!strcmp(leaf,"README")||(n>=3&&!strcmp(leaf+n-3,".so")))C(row.flags==BW_MANIFEST_STREAM);
        }}
        unsigned before=context.requests;struct bw_source_companion_namespace *forbidden=NULL;C(bw_source_provider_companion_namespace(&legacy,&forbidden,NULL)<0&&!forbidden&&context.requests==before);bw_source_provider_destroy(&legacy);C(!context.open);
    }
    free(image);free(input.bytes);
    printf("{\"passed\":true,\"checks\":%u,\"valid\":%s,\"actualProviderPreparation\":true,\"actualTypedNamespace\":true,\"transportOnlyModeled\":true}\n",checks,valid?"true":"false");return 0;
}
