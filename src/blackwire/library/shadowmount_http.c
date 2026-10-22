#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "shadowmount_client.h"
#include "native_document.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static double now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return -1;return (double)t.tv_sec+(double)t.tv_nsec/1e9;}
static int current(const struct bw_sm_connection *c,double deadline){double t=now();return !c||!c->current||c->current(c->opaque)||t<0||t>=deadline?-1:0;}
static int wait_socket(int fd,short events,const struct bw_sm_connection *c,double deadline){
    while(!current(c,deadline)){struct pollfd p={.fd=fd,.events=events};int rc=poll(&p,1,100);if(rc<0&&errno==EINTR)continue;if(rc<0||p.revents&POLLNVAL)return -1;if(rc>0&&(p.revents&(events|POLLERR|POLLHUP)))return 0;}return -1;
}
static int send_bytes(int fd,const char *bytes,size_t size,const struct bw_sm_connection *c,double deadline){
    size_t at=0;while(at<size){if(current(c,deadline)||wait_socket(fd,POLLOUT,c,deadline))return -1;
#ifdef MSG_NOSIGNAL
        ssize_t n=send(fd,bytes+at,size-at,MSG_NOSIGNAL);
#else
        ssize_t n=send(fd,bytes+at,size-at,0);
#endif
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)return -1;at+=(size_t)n;
    }return 0;
}
static int header(const char *bytes,size_t size,size_t maximum,size_t *body_size,unsigned *http){
    if(size<16||size>8192||memchr(bytes,0,size))return -1;char text[8193];memcpy(text,bytes,size);text[size]=0;
    char *line=strstr(text,"\r\n");if(!line)return -1;*line=0;
    if(strlen(text)<12||(strncmp(text,"HTTP/1.0 ",9)&&strncmp(text,"HTTP/1.1 ",9))||
       text[9]<'1'||text[9]>'5'||text[10]<'0'||text[10]>'9'||text[11]<'0'||text[11]>'9'||(text[12]&&text[12]!=' '))return -1;
    unsigned status=(unsigned)(text[9]-'0')*100u+(unsigned)(text[10]-'0')*10u+(unsigned)(text[11]-'0');
    unsigned length_seen=0,type_seen=0;size_t length=0;char *at=line+2;
    while(*at){line=strstr(at,"\r\n");if(!line)return -1;*line=0;if(!*at){if(line+2!=text+size)return -1;break;}
        if(*at==' '||*at=='\t')return -1;char *colon=strchr(at,':');if(!colon||colon==at)return -1;
        for(char *p=at;p<colon;p++)if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||(*p>='0'&&*p<='9')||*p=='-'))return -1;
        *colon++=0;while(*colon==' '||*colon=='\t')colon++;char *end=colon+strlen(colon);while(end>colon&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
        for(char *p=colon;p<end;p++)if((unsigned char)*p<32&&*p!='\t')return -1;
        if(!strcasecmp(at,"Transfer-Encoding"))return -1;
        if(!strcasecmp(at,"Content-Length")){
            if(length_seen++||!*colon)return -1;for(char *p=colon;*p;p++){unsigned d=(unsigned char)*p-'0';if(d>9||length>(maximum-d)/10)return -1;length=length*10+d;}
            if(!length||length>maximum)return -1;
        }else if(!strcasecmp(at,"Content-Type")){
            if(type_seen++||strncasecmp(colon,"application/json",16)||(colon[16]&&colon[16]!=';'&&colon[16]!=' '))return -1;
        }
        at=line+2;
    }
    if(length_seen!=1||type_seen!=1)return -1;*body_size=length;*http=status;return 0;
}
/* All callers select a constant route and construct a bounded body locally.
 * Keeping one transport preserves the registration client's no-retry framing
 * rules for the reconciler instead of introducing another network client. */
static int exchange_status(const struct bw_sm_connection *c,const char *route,const char *body,size_t maximum,struct bw_sm_document *out,unsigned *http){
    if(!out||!http)return -1;memset(out,0,sizeof *out);*http=0;double start=now();if(start<0||current(c,start+10)||!route||!body||maximum>BW_NATIVE_DOCUMENT_LIMIT)return -1;
    char request[2560];unsigned port=c->port?c->port:10101;int n=snprintf(request,sizeof request,"POST %s HTTP/1.0\r\nHost: 127.0.0.1:%u\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",route,port,strlen(body),body);
    if(n<0||(size_t)n>=sizeof request)return -1;
    int fd=socket(AF_INET,SOCK_STREAM,0),result=-1;unsigned char *response=NULL;if(fd<0)return -1;
    int flags=fcntl(fd,F_GETFL,0);if(flags<0||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,FD_CLOEXEC))goto done;
#ifdef SO_NOSIGPIPE
    int yes=1;if(setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes))goto done;
#endif
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),.sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    double deadline=start+10;int connected=connect(fd,(struct sockaddr *)&address,sizeof address);
    if(connected<0&&errno!=EINPROGRESS)goto done;
    if(connected<0){int error=0;socklen_t length=sizeof error;if(wait_socket(fd,POLLOUT,c,deadline)||getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)||error)goto done;}
    if(send_bytes(fd,request,(size_t)n,c,deadline))goto done;
    size_t capacity=8192+maximum+1,used=0,head=0,body_size=0;unsigned status=0;
    response=malloc(capacity);if(!response)goto done;
    for(;;){
        if(current(c,deadline)||wait_socket(fd,POLLIN,c,deadline))goto done;
        ssize_t got=recv(fd,response+used,capacity-used,0);if(got<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(got<0)goto done;if(!got)break;used+=(size_t)got;
        if(!head){
            for(size_t i=3;i<used&&i<8192;i++)if(!memcmp(response+i-3,"\r\n\r\n",4)){head=i+1;break;}
            if(head){if(header((const char *)response,head,maximum,&body_size,&status))goto done;}
            else if(used>=8192)goto done;
        }
        if((head&&used>head+body_size)||used==capacity)goto done;
    }
    if(!head||used!=head+body_size||current(c,deadline))goto done;
    memmove(response,response+head,body_size);response[body_size]=0;
    out->bytes=response;out->size=body_size;response=NULL;*http=status;result=0;
done:
    free(response);if(close(fd)){bw_sm_document_clear(out);*http=0;result=-1;}return result;
}
static int exchange(const struct bw_sm_connection *c,const char *route,const char *body,size_t maximum,struct bw_sm_document *out){
    unsigned http=0;int rc=exchange_status(c,route,body,maximum,out,&http);
    if(!rc&&http!=200){bw_sm_document_clear(out);rc=-1;}return rc;
}
void bw_sm_document_clear(struct bw_sm_document *out){if(out){free(out->bytes);memset(out,0,sizeof *out);}}
int bw_sm_post(const struct bw_sm_connection *c,enum bw_sm_operation kind,const char *root,struct bw_sm_reply *out){
    static const char *routes[]={"/api/v1/version","/api/v1/manual/list","/api/v1/manual/add","/api/v1/scan","/api/v1/manual/remove"};
    if(!out)return -1;memset(out,0,sizeof *out);if(kind<BW_SM_VERSION||kind>BW_SM_MANUAL_REMOVE||!root||strnlen(root,2048)>=2048)return -1;
    char body[2200]="{}";if(kind==BW_SM_MANUAL_ADD||kind==BW_SM_MANUAL_REMOVE){
        if(root[0]!='/')return -1;for(const unsigned char *p=(const unsigned char *)root;*p;p++)if(*p<32||*p>=127||*p=='"'||*p=='\\')return -1;
        int n=snprintf(body,sizeof body,"{\"path\":\"%s\"}",root);if(n<0||(size_t)n>=sizeof body)return -1;
    }else if(kind==BW_SM_SCAN)strcpy(body,"{\"reset_attempts\":false}");
    struct bw_sm_document raw={0};int result=exchange(c,routes[kind],body,kind==BW_SM_MANUAL_LIST?BW_NATIVE_DOCUMENT_LIMIT:16384,&raw);
    if(!result)result=bw_sm_reply_parse(kind,root,raw.bytes,raw.size,out);bw_sm_document_clear(&raw);return result;
}
int bw_sm_observe(const struct bw_sm_connection *c,enum bw_sm_observation kind,struct bw_sm_document *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(kind==BW_SM_OBSERVE_GAMES)return exchange(c,"/api/v1/games","{\"include_size\":false}",BW_NATIVE_DOCUMENT_LIMIT,out);
    if(kind==BW_SM_OBSERVE_MANUAL_PATHS)return exchange(c,"/api/v1/manual/list","{}",BW_NATIVE_DOCUMENT_LIMIT,out);
    if(kind==BW_SM_OBSERVE_SETTINGS)return exchange(c,"/api/v1/settings","{}",BW_NATIVE_DOCUMENT_LIMIT,out);
    return -1;
}
int bw_sm_uninstall(const struct bw_sm_connection *c,const char title[10],struct bw_sm_uninstall_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    char body[40];int n=snprintf(body,sizeof body,"{\"title_id\":\"%s\"}",title);if(n<0||(size_t)n>=sizeof body)return -1;
    struct bw_sm_document raw={0};unsigned http=0;int result=exchange_status(c,"/api/v1/games/uninstall",body,16384,&raw,&http);
    if(!result){
        if(http==200){result=bw_sm_uninstall_reply_parse(title,raw.bytes,raw.size,out);if(!result){out->response_complete=1;out->http_status=200;}}
        else{(void)bw_sm_uninstall_error_parse(http,raw.bytes,raw.size,out);result=-1;}
    }bw_sm_document_clear(&raw);return result;
}
int bw_sm_unmount(const struct bw_sm_connection *c,const char title[10],struct bw_sm_unmount_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!title||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    char body[40];int n=snprintf(body,sizeof body,"{\"title_id\":\"%s\"}",title);if(n<0||(size_t)n>=sizeof body)return -1;
    struct bw_sm_document raw={0};unsigned http=0;int rc=exchange_status(c,"/api/v1/games/unmount",body,16384,&raw,&http);
    if(!rc)rc=bw_sm_unmount_reply_parse(title,http,raw.bytes,raw.size,out);bw_sm_document_clear(&raw);return rc;
}

struct media_parse {struct bw_sm_media_reply value;unsigned seen,failure;};
static int media_integer(const void *raw,size_t n,int32_t *out){
    const unsigned char *p=raw;unsigned negative=0;uint64_t value=0;
    if(!n)return -1;if(*p=='-'){negative=1;p++;n--;}
    if(!n||(n>1&&*p=='0'))return -1;
    uint64_t maximum=negative?UINT64_C(2147483648):INT32_MAX;
    for(size_t i=0;i<n;i++){unsigned d=p[i]-'0';if(d>9||value>(maximum-d)/10)return -1;value=value*10+d;}
    *out=negative?(int32_t)(-(int64_t)value):(int32_t)value;return 0;
}
static int media_bool(const void *raw,size_t n,unsigned *out){
    if(n==4&&!memcmp(raw,"true",4)){*out=1;return 0;}if(n==5&&!memcmp(raw,"false",5)){*out=0;return 0;}return -1;
}
static int media_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct media_parse *p=opaque;unsigned bit=0;
#define MEDIA_KEY(s) (kn==sizeof(s)-1&&!memcmp(key,s,sizeof(s)-1))
    if(MEDIA_KEY("status"))bit=1;else if(MEDIA_KEY("registered"))bit=2;else if(MEDIA_KEY("title_id"))bit=4;
    else if(MEDIA_KEY("registration_attempted"))bit=8;else if(MEDIA_KEY("platform_status"))bit=16;
    else if(p->failure&&MEDIA_KEY("reason"))bit=32;else if(p->failure&&MEDIA_KEY("error"))bit=64;else return -1;
#undef MEDIA_KEY
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1)return media_integer(raw,n,&p->value.api_status)||p->value.api_status<0||(!p->failure&&p->value.api_status)||
        (p->failure&&!p->value.api_status)?-1:0;
    if(bit==2)return media_bool(raw,n,&p->value.registered)||(!p->failure&&!p->value.registered)?-1:0;
    if(bit==4){char title[10];return bw_document_string(raw,n,title,sizeof title)||strcmp(title,"PSBW50001")?-1:0;}
    if(bit==8)return media_bool(raw,n,&p->value.registration_attempted);
    if(bit==32||bit==64){char *out=bit==32?p->value.reason:p->value.error;size_t cap=bit==32?sizeof p->value.reason:sizeof p->value.error;
        if(bw_document_string(raw,n,out,cap)||!*out)return -1;return strspn(out,"abcdefghijklmnopqrstuvwxyz0123456789_")!=strlen(out)?-1:0;}
    if(n==4&&!memcmp(raw,"null",4))return 0;
    p->value.platform_status_present=1;return media_integer(raw,n,&p->value.platform_status);
}
int bw_sm_media_reply_parse(const void *bytes,size_t size,struct bw_sm_media_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);struct media_parse p={0};
    if(!bytes||!size||size>16384||bw_document_fields(bytes,size,media_field,&p)||p.seen!=31)return -1;
    *out=p.value;return 0;
}
struct media_caps {unsigned fields,items,found;};
static int media_cap_item(void *opaque,const void *raw,size_t n){
    struct media_caps *p=opaque;char value[96];if(++p->items>128||bw_document_string(raw,n,value,sizeof value))return -1;
    if(!strcmp(value,"register_blackwire_media")&&p->found++)return -1;return 0;
}
static int media_cap_field(void *opaque,const char *key,size_t kn,const void *raw,size_t n){
    struct media_caps *p=opaque;if(kn!=12||memcmp(key,"capabilities",12))return 0;
    if(p->fields++)return -1;return bw_document_array(raw,n,media_cap_item,p);
}
int bw_sm_media_capability(const struct bw_sm_connection *c){
    struct bw_sm_document raw={0};struct bw_sm_reply version={0};struct media_caps caps={0};
    int result=exchange(c,"/api/v1/version","{}",16384,&raw);
    if(!result){
        if(bw_sm_reply_parse(BW_SM_VERSION,"",raw.bytes,raw.size,&version)||
           bw_document_fields(raw.bytes,raw.size,media_cap_field,&caps)||caps.fields!=1)result=-1;
        else result=caps.found==1?0:-2;
    }
    bw_sm_document_clear(&raw);return result;
}
int bw_sm_media_register(const struct bw_sm_connection *c,struct bw_sm_media_reply *out){
    if(!out)return -1;memset(out,0,sizeof *out);struct bw_sm_document raw={0};unsigned http=0;
    int result=exchange_status(c,"/api/v1/blackwire-media/register","{}",16384,&raw,&http);
    if(!result){
        if(http==200){result=bw_sm_media_reply_parse(raw.bytes,raw.size,out);if(!result)out->response_complete=1;}
        else {struct media_parse failure={.failure=1};
            if((http==409||http==503)&&!bw_document_fields(raw.bytes,raw.size,media_field,&failure)&&failure.seen==127&&
               !strcmp(failure.value.reason,failure.value.error)){*out=failure.value;out->response_complete=1;}
            result=-1;}
        out->http_status=http;
    }
    bw_sm_document_clear(&raw);return result;
}
