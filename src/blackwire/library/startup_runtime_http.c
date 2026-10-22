#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "startup_runtime_control_internal.h"
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
static int current(const struct bw_src_connection *c,double deadline){double t=now();return !c||!c->current||c->current(c->opaque)||t<0||t>=deadline?-1:0;}
static int wait_socket(int fd,short events,const struct bw_src_connection *c,double deadline){
    while(!current(c,deadline)){struct pollfd p={.fd=fd,.events=events};int rc=poll(&p,1,100);if(rc<0&&errno==EINTR)continue;if(rc<0||p.revents&POLLNVAL)return -1;if(rc>0&&(p.revents&(events|POLLERR|POLLHUP)))return 0;}return -1;
}
static int send_bytes(int fd,const char *bytes,size_t size,const struct bw_src_connection *c,double deadline){
    size_t at=0;while(at<size){if(current(c,deadline)||wait_socket(fd,POLLOUT,c,deadline))return -1;
#ifdef MSG_NOSIGNAL
        ssize_t n=send(fd,bytes+at,size-at,MSG_NOSIGNAL);
#else
        ssize_t n=send(fd,bytes+at,size-at,0);
#endif
        if(n<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;if(n<=0)return -1;at+=(size_t)n;
    }return 0;
}
static int header(const char *bytes,size_t size,size_t maximum,size_t *body_size){
    if(size<16||size>8192||memchr(bytes,0,size))return -1;char text[8193];memcpy(text,bytes,size);text[size]=0;
    char *line=strstr(text,"\r\n");if(!line)return -1;*line=0;
    if(strlen(text)<12||(strncmp(text,"HTTP/1.0 ",9)&&strncmp(text,"HTTP/1.1 ",9))||strncmp(text+9,"200",3)||(text[12]&&text[12]!=' '))return -1;
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
    if(length_seen!=1||type_seen!=1)return -1;*body_size=length;return 0;
}
int bw_src_http(const struct bw_src_connection *c,enum bw_src_operation kind,const char *root,const char *title,struct bw_src_reply *out){
    static const char *routes[]={"/api/v1/version","/api/v1/games/info","/api/v1/games/unmount","/api/v1/settings","/api/v1/manual/list"};
    if(!out)return -1;memset(out,0,sizeof *out);double start=now();
    if(start<0||current(c,start+10)||kind<BW_SRC_VERSION||kind>BW_SRC_MANUAL||!root||!title||strnlen(root,128)>=128||strnlen(title,10)!=9||memcmp(title,"PPSA",4))return -1;
    for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')return -1;
    char body[64]="{}",request[512];if(kind==BW_SRC_INFO||kind==BW_SRC_UNMOUNT){int n=snprintf(body,sizeof body,"{\"title_id\":\"%s\"}",title);if(n<0||(size_t)n>=sizeof body)return -1;}
    unsigned port=c->port?c->port:10101;int n=snprintf(request,sizeof request,"POST %s HTTP/1.0\r\nHost: 127.0.0.1:%u\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",routes[kind],port,strlen(body),body);
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
    size_t maximum=(kind==BW_SRC_SETTINGS||kind==BW_SRC_MANUAL)?BW_SRC_DOCUMENT_LIMIT:16384,capacity=8192+maximum+1,used=0,head=0,body_size=0;
    response=malloc(capacity);if(!response)goto done;
    for(;;){
        if(current(c,deadline)||wait_socket(fd,POLLIN,c,deadline))goto done;
        ssize_t got=recv(fd,response+used,capacity-used,0);if(got<0&&(errno==EINTR||errno==EAGAIN||errno==EWOULDBLOCK))continue;
        if(got<0)goto done;if(!got)break;used+=(size_t)got;
        if(!head){
            for(size_t i=3;i<used&&i<8192;i++)if(!memcmp(response+i-3,"\r\n\r\n",4)){head=i+1;break;}
            if(head){if(header((const char *)response,head,maximum,&body_size))goto done;}
            else if(used>=8192)goto done;
        }
        if((head&&used>head+body_size)||used==capacity)goto done;
    }
    if(!head||used!=head+body_size||current(c,deadline))goto done;
    result=bw_src_reply_parse(kind,root,title,response+head,body_size,out);
done:
    free(response);close(fd);return result;
}
