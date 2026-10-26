/* Protocol and actual compressed DATA binder tests; synthetic data only. */
#define _POSIX_C_SOURCE 200809L
#include "library/reader_selection.h"
#include "streamreader/container_manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
static unsigned checks,cases;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"contract:%d %s\n",__LINE__,#x);exit(1);}}while(0)
static void repeated(char *p,size_t n,char c){memset(p,c,n);p[n]=0;}
static void rejected(const char *bytes,size_t n){struct bw_reader_selection out;memset(&out,0xab,sizeof out);
    C(bw_reader_selection_parse(&out,bytes,n)<0);struct bw_reader_selection zero={0};C(!memcmp(&out,&zero,sizeof out));cases++;}
static void selection_cases(void){
    for(unsigned kind=BW_PROVIDER_FOLDER;kind<=BW_PROVIDER_CONTAINER;kind++){
        struct bw_reader_selection s={0},parsed;char bytes[4097],again[4097];size_t n=0,m=0;
        strcpy(s.title,"PPSA09999");repeated(s.binding_hex,64,'a');snprintf(s.game,sizeof s.game,"%s_%s",s.title,s.binding_hex);
        strcpy(s.source,kind==BW_PROVIDER_FOLDER?"Original folder":"Original.ffpfsc");repeated(s.run,32,'b');repeated(s.prepared_record_hex,64,'c');
        s.source_kind=kind;s.source_policy=BW_PROVIDER_POLICY_V1;s.eboot_size=1300;s.manifest_count=4;s.manifest_hash=17;
        C(!bw_reader_selection_serialize(&s,bytes,sizeof bytes,&n));C(!strncmp(bytes,kind==1?"version=4\n":"version=5\n",10));
        C(!strstr(bytes,"prepared_policy="));C(!bw_reader_selection_parse(&parsed,bytes,n));
        C(!bw_reader_selection_serialize(&parsed,again,sizeof again,&m)&&n==m&&!memcmp(bytes,again,n));cases++;
        s.prepared_policy=3;strcpy(s.prepared_game,s.game);repeated(s.provenance_hex,64,'d');
        C(!bw_reader_selection_serialize(&s,bytes,sizeof bytes,&n));C(!strncmp(bytes,"version=8\n",10));
        C(strstr(bytes,"prepared_policy=3\n")&&strstr(bytes,"provenance_sha256="));
        C(!bw_reader_selection_parse(&parsed,bytes,n));C(bw_reader_selection_is_projection(&parsed));
        C(!bw_reader_selection_is_derived(&parsed)&&!bw_reader_selection_is_batch(&parsed));C(!memcmp(&s,&parsed,sizeof s));
        C(!bw_reader_selection_serialize(&parsed,again,sizeof again,&m)&&n==m&&!memcmp(bytes,again,n));cases++;
        memcpy(again,bytes,n+1);char *p=strstr(again,"prepared_policy=3");C(p);p[16]='4';rejected(again,n);
        memcpy(again,bytes,n+1);p=strstr(again,"prepared_game=");C(p);p[14]='Q';rejected(again,n);
        memcpy(again,bytes,n+1);p=strstr(again,"provenance_sha256=");C(p);memset(p+18,'0',64);rejected(again,n);
        memcpy(again,bytes,n+1);p=strstr(again,"source_policy=1");C(p);p[14]='2';rejected(again,n);
        memcpy(again,bytes,n+1);p=strstr(again,"source_kind=");C(p);p[12]='9';rejected(again,n);
        memcpy(again,bytes,n+1);again[n-1]=0;rejected(again,n);
        memcpy(again,bytes,n+1);strcpy(again+n,"extra=1\n");rejected(again,n+8);
        rejected(bytes,n-1);
        memcpy(again,bytes,n+1);again[9]='\r';rejected(again,n);
        memcpy(again,bytes,n+1);p=strstr(again,"manifest_count=4");C(p);size_t offset=(size_t)(p-again)+15;
        memmove(again+offset+1,again+offset,n-offset+1);again[offset]='0';rejected(again,n+1);
        memcpy(again,bytes,n+1);p=strstr(again,"source_kind=");C(p);size_t field=(size_t)(strchr(p,'\n')-p)+1;
        C(n+field<sizeof again);memcpy(again+n,p,field);rejected(again,n+field);
        memcpy(again,bytes,n+1);p=strstr(again,"prepared_policy=3\n");C(p);offset=(size_t)(p-again);field=strlen("prepared_policy=3\n");
        memmove(again+offset,again+offset+field,n-offset-field);rejected(again,n-field);
        memcpy(again,bytes,n+1);p=strstr(again,"source_policy=1\n");C(p);p[13]=' ';rejected(again,n);
        struct bw_reader_selection bad=s;bad.prepared_game[0]='Q';memset(again,0xab,sizeof again);m=99;
        C(bw_reader_selection_serialize(&bad,again,sizeof again,&m)<0&&m==0&&again[0]==0);cases++;
        C(bw_reader_selection_serialize(&s,again,10,&m)<0&&m==0&&again[0]==0);cases++;
    }
}
struct input{int fd,changed;unsigned reads,generations;};
static int read_at(void *opaque,uint64_t offset,void *out,size_t n){struct input *p=opaque;p->reads++;
    return pread(p->fd,out,n,(off_t)offset)==(ssize_t)n?0:-1;}
static int current(void *opaque){struct input *p=opaque;p->generations++;return p->changed;}
static void *load(const char *name,size_t *size){int fd=open(name,O_RDONLY);C(fd>=0);struct stat s;C(!fstat(fd,&s)&&s.st_size>0&&s.st_size<1024*1024);
    *size=(size_t)s.st_size;void *p=malloc(*size);C(p&&read(fd,p,*size)==(ssize_t)*size&&!close(fd));return p;}
static void data_cases(const char *image,const char *mapname,int mode){
    struct input io={.fd=open(image,O_RDONLY)};C(io.fd>=0);struct stat st;C(!fstat(io.fd,&st));
    struct bw_container source={0};struct bw_container_ops ops={read_at,current};C(!bw_container_open(&source,&ops,&io,(uint64_t)st.st_size));
    size_t size;void *raw=load(mapname,&size);struct bw_manifest map;C(!bw_manifest_open(&map,raw,size));
    struct bw_container_manifest data={0};int result=bw_container_manifest_bind_streams(&data,&source,&map);
    if(mode==1){C(result==BW_CONTAINER_MANIFEST_MISMATCH);C(!data.files&&!data.count);cases++;}
    else{
        C(!result&&data.count==map.count&&data.allocation_bytes<=4u*1024u*1024u);unsigned streams=0;
        for(uint32_t i=0;i<map.count;i++){
            struct bw_manifest_record row;C(!bw_manifest_get(&map,i,&row));
            const struct bw_container_file *token=bw_container_manifest_file(&data,i);
            if(row.flags==BW_MANIFEST_LOCAL)C(!token);
            else{C(token&&token->size==row.size);unsigned char bytes[16];C(!bw_container_file_read(token,0,bytes,sizeof bytes));
                for(unsigned j=0;j<sizeof bytes;j++)C(bytes[j]==(unsigned char)j);streams++;}
        }
        C(streams==(mode==2?0u:1u));cases++;
        struct bw_container_manifest full={0};C(bw_container_manifest_bind(&full,&source,&map)==BW_CONTAINER_MANIFEST_MISMATCH&&!full.files);cases++;
        if(streams){
            uint32_t id=0;while(id<map.count&&!data.files[id].owner)id++;C(id<map.count);
            struct bw_container_file copied=data.files[id];unsigned char bytes[16];
            io.changed=BW_CONTAINER_CANCELED;C(!bw_container_manifest_file(&data,id)&&!data.failed);
            io.changed=0;C(bw_container_manifest_file(&data,id)&&!bw_container_file_read(&copied,0,bytes,sizeof bytes));cases++;
            C(!bw_container_open(&source,&ops,&io,(uint64_t)st.st_size));
            C(!bw_container_manifest_file(&data,id)&&bw_container_file_read(&copied,0,bytes,sizeof bytes)<0);cases++;
            bw_container_manifest_destroy(&data);C(!bw_container_manifest_bind_streams(&data,&source,&map));
        }
        io.changed=BW_CONTAINER_STALE;
        if(streams)for(uint32_t i=0;i<map.count;i++)C(!bw_container_manifest_file(&data,i));
        bw_container_manifest_destroy(&data);C(!data.files&&!data.count);
        struct bw_container_manifest stale={0};C(bw_container_manifest_bind_streams(&stale,&source,&map)<0&&!stale.files);cases++;
    }
    free(raw);C(!close(io.fd));
}
int main(int argc,char **argv){C(argc==5);selection_cases();data_cases(argv[1],argv[2],0);data_cases(argv[1],argv[3],1);data_cases(argv[1],argv[4],2);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualSelectionAndContainerCode\":true,\"syntheticOnly\":true,\"consoleActions\":0}\n",cases,checks);return 0;}
