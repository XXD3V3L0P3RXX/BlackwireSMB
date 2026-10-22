#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "reader_selection.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int rsel_copy(char *out,size_t capacity,const char *value){
    size_t n=strlen(value);if(!n||n>=capacity)return -1;memcpy(out,value,n+1);return 0;
}
static int rsel_hex(const char *value,size_t size){
    if(strlen(value)!=size)return -1;
    for(size_t i=0;i<size;i++)if(!strchr("0123456789abcdef",value[i]))return -1;
    return 0;
}
static int rsel_number(const char *value,uint64_t *out){
    if(!*value)return -1;
    for(const char *p=value;*p;p++)if(*p<'0'||*p>'9')return -1;
    errno=0;char *end=NULL;unsigned long long n=strtoull(value,&end,10);
    if(errno||!end||*end)return -1;*out=n;return 0;
}
static int rsel_legacy_parse(struct bw_reader_selection *out,const void *input,size_t size){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!input||!size||size>BW_READER_SELECTION_BYTES||memchr(input,0,size)||((const char *)input)[size-1]!='\n')return -1;
    struct bw_reader_selection next={0};char text[BW_READER_SELECTION_BYTES+1];memcpy(text,input,size);text[size]=0;
    unsigned seen=0,version=0;char *at=text;
    while(at<text+size){
        char *end=strchr(at,'\n');if(!end)goto bad;*end=0;if(end>at&&end[-1]=='\r')end[-1]=0;
        char *value=strchr(at,'=');if(!value)goto bad;*value++=0;unsigned bit=0;
        if(!strcmp(at,"version")){bit=1;if(!strcmp(value,"4"))version=4;else if(!strcmp(value,"5"))version=5;else goto bad;}
        else if(!strcmp(at,"run_id")){bit=2;if(rsel_copy(next.run,sizeof next.run,value)||rsel_hex(value,32))goto bad;}
        else if(!strcmp(at,"title_id")){bit=4;if(rsel_copy(next.title,sizeof next.title,value))goto bad;}
        else if(!strcmp(at,"game")){bit=8;if(rsel_copy(next.game,sizeof next.game,value))goto bad;}
        else if(!strcmp(at,"eboot_size")){bit=16;if(rsel_number(value,&next.eboot_size))goto bad;}
        else if(!strcmp(at,"manifest_count")){uint64_t count;bit=32;if(rsel_number(value,&count)||!count||count>BW_MANIFEST_MAX_FILES)goto bad;next.manifest_count=(uint32_t)count;}
        else if(!strcmp(at,"manifest_hash")){bit=64;if(rsel_hex(value,16))goto bad;next.manifest_hash=strtoull(value,NULL,16);}
        else if(!strcmp(at,"source")){bit=128;if(rsel_copy(next.source,sizeof next.source,value)||bw_manifest_validate_path(value,strlen(value)))goto bad;}
        else if(!strcmp(at,"source_binding")){bit=256;if(rsel_copy(next.binding_hex,sizeof next.binding_hex,value)||rsel_hex(value,64))goto bad;}
        else if(!strcmp(at,"prepared_record")){bit=512;if(rsel_copy(next.prepared_record_hex,sizeof next.prepared_record_hex,value)||rsel_hex(value,64))goto bad;}
        else if(!strcmp(at,"source_kind")){bit=1024;if(strcmp(value,"2"))goto bad;next.source_kind=BW_PROVIDER_CONTAINER;}
        else if(!strcmp(at,"source_policy")){bit=2048;if(strcmp(value,"1"))goto bad;next.source_policy=BW_PROVIDER_POLICY_V1;}
        else goto bad;
        if(seen&bit)goto bad;seen|=bit;at=end+1;
    }
    if(version==4){if(seen!=1023)goto bad;next.source_kind=BW_PROVIDER_FOLDER;next.source_policy=BW_PROVIDER_POLICY_V1;}
    else if(version!=5||seen!=4095||next.source_kind!=BW_PROVIDER_CONTAINER||next.source_policy!=BW_PROVIDER_POLICY_V1)goto bad;
    if(strlen(next.title)!=9||memcmp(next.title,"PPSA",4)||strlen(next.game)!=74||
       memcmp(next.game,next.title,9)||next.game[9]!='_'||strcmp(next.game+10,next.binding_hex)||
       !next.eboot_size||next.eboot_size>UINT64_C(2147483648))goto bad;
    for(unsigned i=4;i<9;i++)if(next.title[i]<'0'||next.title[i]>'9')goto bad;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++)nonzero|=next.run[i]!='0';if(!nonzero)goto bad;
    *out=next;return 0;
bad:
    return -1;
}
static int rsel_legacy_serialize(const struct bw_reader_selection *s,void *output,size_t capacity,size_t *size){
    if(size)*size=0;
    if(output&&capacity)memset(output,0,capacity);
    if(!s||!output||!capacity||!size||s->source_policy!=BW_PROVIDER_POLICY_V1||
       (s->source_kind!=BW_PROVIDER_FOLDER&&s->source_kind!=BW_PROVIDER_CONTAINER)||
       !memchr(s->source,0,sizeof s->source)||!memchr(s->game,0,sizeof s->game)||!memchr(s->title,0,sizeof s->title)||
       !memchr(s->run,0,sizeof s->run)||!memchr(s->binding_hex,0,sizeof s->binding_hex)||!memchr(s->prepared_record_hex,0,sizeof s->prepared_record_hex))return -1;
    char text[BW_READER_SELECTION_BYTES+1];struct bw_reader_selection checked;
    const unsigned version=s->source_kind==BW_PROVIDER_FOLDER?4:5;
    int n=snprintf(text,sizeof text,"version=%u\nrun_id=%s\ntitle_id=%s\ngame=%s\neboot_size=%llu\nmanifest_count=%u\nmanifest_hash=%016llx\nsource=%s\nsource_binding=%s\nprepared_record=%s\n",
        version,s->run,s->title,s->game,(unsigned long long)s->eboot_size,s->manifest_count,(unsigned long long)s->manifest_hash,s->source,s->binding_hex,s->prepared_record_hex);
    if(n<=0||(size_t)n>=sizeof text)return -1;
    if(version==5){int added=snprintf(text+n,sizeof text-(size_t)n,"source_kind=%u\nsource_policy=%u\n",s->source_kind,s->source_policy);
        if(added<=0||(size_t)added>=sizeof text-(size_t)n)return -1;n+=added;}
    if((size_t)n>BW_READER_SELECTION_BYTES||(size_t)n>=capacity||rsel_legacy_parse(&checked,text,(size_t)n)||
       strcmp(checked.source,s->source)||strcmp(checked.game,s->game)||strcmp(checked.title,s->title)||strcmp(checked.run,s->run)||
       strcmp(checked.binding_hex,s->binding_hex)||strcmp(checked.prepared_record_hex,s->prepared_record_hex)||
       checked.eboot_size!=s->eboot_size||checked.manifest_count!=s->manifest_count||checked.manifest_hash!=s->manifest_hash||
       checked.source_kind!=s->source_kind||checked.source_policy!=s->source_policy)return -1;
    memcpy(output,text,(size_t)n+1);*size=(size_t)n;return 0;
}
static int rsel_identity(const struct stat *a,const struct stat *b){
    return S_ISREG(b->st_mode)&&b->st_nlink==1&&(b->st_mode&07777)==0600&&a->st_dev==b->st_dev&&a->st_ino==b->st_ino&&
           a->st_size==b->st_size&&a->st_mtime==b->st_mtime&&a->st_ctime==b->st_ctime;
}
int bw_reader_selection_load(struct bw_reader_selection *out,const char *path){
    if(!out)return -1;memset(out,0,sizeof *out);char canonical[PATH_MAX],bytes[BW_READER_SELECTION_BYTES+1],again[BW_READER_SELECTION_BYTES];
    if(!path||path[0]!='/'||strnlen(path,1024)>=1024||!realpath(path,canonical)||strcmp(path,canonical))return -1;
    int fd=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(fd<0)return -1;
    struct stat before,held,named;int result=-1;
    if(fstat(fd,&before)||!rsel_identity(&before,&before)||before.st_size<=0||(uint64_t)before.st_size>BW_READER_SELECTION_BYTES)goto done;
    size_t size=(size_t)before.st_size;
    if(pread(fd,bytes,size+1,0)!=(ssize_t)size||fstat(fd,&held)||lstat(path,&named)||!rsel_identity(&before,&held)||!rsel_identity(&held,&named)||
       pread(fd,again,size,0)!=(ssize_t)size||memcmp(bytes,again,size)||fstat(fd,&held)||lstat(path,&named)||
       !rsel_identity(&before,&held)||!rsel_identity(&held,&named)||!realpath(path,canonical)||strcmp(path,canonical)||bw_reader_selection_parse(out,bytes,size))goto done;
    result=0;
done:
    if(close(fd))result=-1;if(result)memset(out,0,sizeof *out);return result;
}


static int rsel_nonzero(const char *p,size_t n){for(size_t i=0;i<n;i++)if(p[i]!='0')return 1;return 0;}
static int rsel_derived_bytes(const struct bw_reader_selection *s,char out[BW_READER_SELECTION_BYTES+1],size_t *size){
    if(!s||(s->prepared_policy!=1&&s->prepared_policy!=2)||s->source_kind!=BW_PROVIDER_FOLDER||s->source_policy!=1||
       !memchr(s->prepared_game,0,sizeof s->prepared_game)||!memchr(s->provenance_hex,0,sizeof s->provenance_hex)||
       strlen(s->prepared_game)!=74||memcmp(s->prepared_game,s->title,9)||s->prepared_game[9]!='_'||rsel_hex(s->prepared_game+10,64)||
       rsel_hex(s->provenance_hex,64)||!rsel_nonzero(s->provenance_hex,64)||!memchr(s->prepared_record_hex,0,sizeof s->prepared_record_hex)||
       rsel_hex(s->prepared_record_hex,64)||!rsel_nonzero(s->prepared_record_hex,64)||
       s->manifest_count>(s->prepared_policy==1?4097u:8193u))return -1;
    struct bw_reader_selection legacy=*s;legacy.prepared_policy=0;memset(legacy.prepared_game,0,sizeof legacy.prepared_game);memset(legacy.provenance_hex,0,sizeof legacy.provenance_hex);
    char common[BW_READER_SELECTION_BYTES+1];size_t n=0;if(rsel_legacy_serialize(&legacy,common,sizeof common,&n)||n<10||memcmp(common,"version=4\n",10)||!strcmp(s->game,s->prepared_game))return -1;
    int wrote=snprintf(out,BW_READER_SELECTION_BYTES+1,"version=%u\n%ssource_kind=1\nsource_policy=1\nprepared_policy=%u\nprepared_game=%s\nprovenance_sha256=%s\n",
        s->prepared_policy==1?6u:7u,common+10,s->prepared_policy,s->prepared_game,s->provenance_hex);
    if(wrote<=0||(size_t)wrote>BW_READER_SELECTION_BYTES)return -1;*size=(size_t)wrote;return 0;
}
int bw_reader_selection_is_derived(const struct bw_reader_selection *s){return s&&s->prepared_policy==1;}
int bw_reader_selection_is_batch(const struct bw_reader_selection *s){return s&&s->prepared_policy==2;}
int bw_reader_selection_serialize(const struct bw_reader_selection *s,void *out,size_t capacity,size_t *size){
    if(size)*size=0;if(out&&capacity)memset(out,0,capacity);
    if(!s||!out||!capacity||!size)return -1;
    if(!s->prepared_policy){if(s->prepared_game[0]||s->provenance_hex[0])return -1;return rsel_legacy_serialize(s,out,capacity,size);}
    char bytes[BW_READER_SELECTION_BYTES+1];size_t n=0;if(rsel_derived_bytes(s,bytes,&n)||n>=capacity)return -1;
    memcpy(out,bytes,n+1);*size=n;return 0;
}
int bw_reader_selection_parse(struct bw_reader_selection *out,const void *input,size_t size){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!input||size<10||(memcmp(input,"version=6\n",10)&&memcmp(input,"version=7\n",10)))return rsel_legacy_parse(out,input,size);
    const unsigned policy=((const char *)input)[8]=='6'?1u:2u;
    if(size>BW_READER_SELECTION_BYTES||memchr(input,0,size)||((const char *)input)[size-1]!='\n')return -1;
    char text[BW_READER_SELECTION_BYTES+1];memcpy(text,input,size);text[size]=0;
    const char *keys[]={"version","run_id","title_id","game","eboot_size","manifest_count","manifest_hash","source","source_binding","prepared_record","source_kind","source_policy","prepared_policy","prepared_game","provenance_sha256"};
    char *values[15],*at=text;size_t common_end=0;
    for(unsigned i=0;i<15;i++){char *end=strchr(at,'\n');if(!end)return -1;*end=0;size_t n=strlen(keys[i]);if(strncmp(at,keys[i],n)||at[n]!='=')return -1;values[i]=at+n+1;if(i==9)common_end=(size_t)(end-text)+1;at=end+1;}
    if(at!=text+size||strcmp(values[0],policy==1?"6":"7")||strcmp(values[10],"1")||strcmp(values[11],"1")||strcmp(values[12],policy==1?"1":"2"))return -1;
    char legacy[BW_READER_SELECTION_BYTES+1];memcpy(legacy,input,common_end);legacy[8]='4';struct bw_reader_selection next;
    if(rsel_legacy_parse(&next,legacy,common_end)||rsel_copy(next.prepared_game,sizeof next.prepared_game,values[13])||rsel_copy(next.provenance_hex,sizeof next.provenance_hex,values[14]))return -1;next.prepared_policy=policy;
    char canonical[BW_READER_SELECTION_BYTES+1];size_t n=0;if(rsel_derived_bytes(&next,canonical,&n)||n!=size||memcmp(canonical,input,size))return -1;*out=next;return 0;
}
