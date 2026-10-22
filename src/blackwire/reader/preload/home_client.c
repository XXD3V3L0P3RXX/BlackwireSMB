/* One fixed loopback request with strict framing and one absolute deadline.
 * SPDX-License-Identifier: GPL-3.0-or-later */
#include "home_client.h"
#include "../../library/home_protocol.h"
#include "../../streamreader/copy_sha256.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#ifndef BW_HOME_CLIENT_HOST_TEST
#include <sys/sysctl.h>
#else
#define CTL_KERN 1
#define KERN_ARND 37
int sysctl(const int *,unsigned,void *,size_t *,const void *,size_t);
#endif
#if !defined(SO_NOSIGPIPE) && !defined(MSG_NOSIGNAL)
#error "A qualified no-SIGPIPE socket path is required"
#endif
#define HOME_US UINT64_C(20000000)
/* Native title socket option; PS5 titles deny F_SETFL and FIONBIO. */
enum {BW_HOME_SO_NBIO=0x1200};
static struct bw_home_client_failure first_failure;
static unsigned response_status;
static int failed(unsigned stage,int64_t raw,int error,unsigned detail){
    if(!first_failure.stage)first_failure=(struct bw_home_client_failure){stage,detail,raw,error,response_status};
    return -1;
}
void bw_home_client_diagnostic(struct bw_home_client_failure *out){if(out)*out=first_failure;}
static int close_fd(int fd){int result=close(fd);int saved=errno;if(result)failed(BW_HC_CLOSE,result,saved,0);return result;}
struct transfer_clock {uint64_t end,last;};
static int clock_now(uint64_t *out){
    struct timespec t;
    int result=clock_gettime(CLOCK_MONOTONIC,&t);int saved=errno;
    if(result)return failed(BW_HC_CLOCK,result,saved,1);
    if(t.tv_sec<0||t.tv_nsec<0||t.tv_nsec>=1000000000L||
       (uint64_t)t.tv_sec>(INT64_MAX-999999u)/1000000u)return failed(BW_HC_CLOCK,0,0,2);
    *out=(uint64_t)t.tv_sec*1000000u+(uint64_t)t.tv_nsec/1000u;return 0;
}
static int clock_current(struct transfer_clock *c,uint64_t *remain){
    uint64_t now;if(clock_now(&now))return -1;
    if(now<c->last)return failed(BW_HC_DEADLINE,0,0,1);
    if(now>=c->end)return failed(BW_HC_DEADLINE,0,0,2);
    c->last=now;if(remain)*remain=c->end-now;return 0;
}
static int wait_fd(int fd,short events,struct transfer_clock *c,unsigned step){
    for(;;){
        uint64_t remain;if(clock_current(c,&remain))return -1;
        int timeout=(int)((remain+999u)/1000u);if(timeout>1000)timeout=1000;
        struct pollfd p={fd,events,0};int n=poll(&p,1,timeout);int saved=errno;
        if(n<0&&saved==EINTR)continue;
        if(n==0)continue; /* Never restart the absolute deadline. */
        if(n!=1||(p.revents&POLLNVAL)||!(p.revents&(events|POLLERR|POLLHUP)))
            return failed(BW_HC_POLL,n,n<0?saved:0,(step<<16)|(unsigned)(unsigned short)p.revents);
        if(clock_current(c,NULL))return -1;
        return 0;
    }
}
static int connection(struct transfer_clock *c){
    if(clock_current(c,NULL))return -1;
    int fd=socket(AF_INET,SOCK_STREAM,0);int saved=errno;if(fd<0)return failed(BW_HC_SOCKET,fd,saved,0);
    const int enabled=1;
    int changed=setsockopt(fd,SOL_SOCKET,BW_HOME_SO_NBIO,&enabled,sizeof enabled);saved=errno;
    if(changed){failed(BW_HC_NONBLOCK,changed,saved,0);(void)close_fd(fd);return -1;}
#ifdef SO_NOSIGPIPE
    int no_signal=1;
    int set=setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&no_signal,sizeof no_signal);saved=errno;
    if(set){failed(BW_HC_NOSIGPIPE,set,saved,0);(void)close_fd(fd);return -1;}
#endif
    struct sockaddr_in address={0};address.sin_family=AF_INET;address.sin_port=htons(5918);address.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(clock_current(c,NULL)){(void)close_fd(fd);return -1;}
    int result=connect(fd,(struct sockaddr *)&address,sizeof address);saved=errno;
    if(result<0&&saved!=EINPROGRESS){failed(BW_HC_CONNECT,result,saved,0);(void)close_fd(fd);return -1;}
    if(result<0){
        int error=0;socklen_t size=sizeof error;
        if(wait_fd(fd,POLLOUT,c,BW_HC_IO_CONNECT)){(void)close_fd(fd);return -1;}
        int got=getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&size);saved=errno;
        if(got){failed(BW_HC_CONNECT_RESULT,got,saved,1);(void)close_fd(fd);return -1;}
        if(size!=sizeof error){failed(BW_HC_CONNECT_RESULT,(int64_t)size,0,2);(void)close_fd(fd);return -1;}
        if(error){failed(BW_HC_CONNECT_RESULT,error,0,3);(void)close_fd(fd);return -1;}
    }
    if(clock_current(c,NULL)){(void)close_fd(fd);return -1;}return fd;
}
static int send_all(int fd,const void *raw,size_t size,struct transfer_clock *c,unsigned step){
    const unsigned char *bytes=raw;
    while(size){
        if(wait_fd(fd,POLLOUT,c,step))return -1;
        ssize_t sent=send(fd,bytes,size,
#ifdef MSG_NOSIGNAL
            MSG_NOSIGNAL
#else
            0
#endif
        );
        int saved=errno;
        if(sent<0&&(saved==EINTR||saved==EAGAIN||saved==EWOULDBLOCK))continue;
        if(sent<=0||(size_t)sent>size)return failed(BW_HC_SEND,sent,sent<0?saved:0,step);bytes+=(size_t)sent;size-=(size_t)sent;
    }
    return clock_current(c,NULL);
}
static ssize_t receive(int fd,void *out,size_t size,struct transfer_clock *c,unsigned step){
    for(;;){
        if(wait_fd(fd,POLLIN,c,step))return -1;
        ssize_t got=recv(fd,out,size,0);int saved=errno;
        if(got<0&&(saved==EINTR||saved==EAGAIN||saved==EWOULDBLOCK))continue;
        if(got<0)failed(BW_HC_RECEIVE,got,saved,step);
        return got;
    }
}
static int token_char(unsigned char c){return c&&((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||strchr("!#$%&'*+-.^_`|~",c)!=NULL);}
static int json_type(char *value){
    while(*value==' '||*value=='\t')value++;
    char *end=value+strlen(value);while(end>value&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
    return strcasecmp(value,"application/json");
}
static int transfer(const void *body,size_t size,char out[BW_HOME_REQUEST_LIMIT+1],size_t *used,struct transfer_clock *clock){
    *used=0;out[0]=0;int fd=connection(clock);if(fd<0)return -1;
    char request[512],response[2048];size_t count=0,length=0;unsigned lengths=0,types=0;int result=-1;
    int n=snprintf(request,sizeof request,"POST /api/reader/bootstrap HTTP/1.1\r\nHost: 127.0.0.1:5918\r\nConnection: close\r\nContent-Type: application/json\r\nContent-Length: %zu\r\nX-Blackwire-Reader: home/1\r\n\r\n",size);
    if(n<=0||(size_t)n>=sizeof request){failed(BW_HC_REQUEST_FORMAT,n,0,0);goto done;}
    if(send_all(fd,request,(size_t)n,clock,BW_HC_IO_SEND_HEADER)||send_all(fd,body,size,clock,BW_HC_IO_SEND_BODY))goto done;
    while(count+1<sizeof response){
        ssize_t got=receive(fd,response+count,1,clock,BW_HC_IO_HEADER);
        if(got!=1){failed(BW_HC_HTTP_HEADER,got,0,1);goto done;}
        if(!response[count]){failed(BW_HC_HTTP_HEADER,0,0,2);goto done;}
        count++;if(count>=4&&!memcmp(response+count-4,"\r\n\r\n",4))break;
    }
    response[count]=0;
    /* The existing resident emits HTTP/1.0. HTTP/1.1 is also accepted, with
     * exactly framed JSON and a real close in either case. No redirects. */
    if(count>=13&&response[9]>='0'&&response[9]<='9'&&response[10]>='0'&&response[10]<='9'&&response[11]>='0'&&response[11]<='9'&&response[12]==' ')
        response_status=(unsigned)(response[9]-'0')*100u+(unsigned)(response[10]-'0')*10u+(unsigned)(response[11]-'0');
    if(count<17||memcmp(response+count-4,"\r\n\r\n",4)){failed(BW_HC_HTTP_HEADER,(int64_t)count,0,3);goto done;}
    if(memcmp(response,"HTTP/1.0 ",9)&&memcmp(response,"HTTP/1.1 ",9)){failed(BW_HC_HTTP_STATUS,0,0,1);goto done;}
    if(memcmp(response+9,"202 ",4)){failed(BW_HC_HTTP_STATUS,response_status,0,2);goto done;}
    char *line=strstr(response,"\r\n");if(!line){failed(BW_HC_HTTP_HEADER,0,0,4);goto done;}
    for(char *p=response+13;p<line;p++)if((unsigned char)*p<32||(unsigned char)*p==127){failed(BW_HC_HTTP_HEADER,0,0,5);goto done;}
    while(line[2]!='\r'){
        line+=2;char *next=strstr(line,"\r\n");if(!next){failed(BW_HC_HTTP_HEADER,0,0,6);goto done;}*next=0;
        char *colon=strchr(line,':');if(!colon||colon==line){failed(BW_HC_HTTP_HEADER,0,0,7);goto done;}
        for(char *p=line;p<colon;p++)if(!token_char((unsigned char)*p)){failed(BW_HC_HTTP_HEADER,0,0,8);goto done;}
        for(char *p=colon+1;p<next;p++)if(((unsigned char)*p<32&&*p!='\t')||(unsigned char)*p==127){failed(BW_HC_HTTP_HEADER,0,0,9);goto done;}
        *colon=0;
        if(!strcasecmp(line,"Transfer-Encoding")){failed(BW_HC_HTTP_HEADER,0,0,10);goto done;}
        if(!strcasecmp(line,"Content-Type")){
            if(types++){failed(BW_HC_CONTENT_TYPE,0,0,1);goto done;}
            if(json_type(colon+1)){failed(BW_HC_CONTENT_TYPE,0,0,2);goto done;}
        }
        if(!strcasecmp(line,"Content-Length")){
            char *value=colon+1;while(*value==' '||*value=='\t')value++;
            if(!*value){failed(BW_HC_CONTENT_LENGTH,0,0,1);goto done;}
            if(lengths++){failed(BW_HC_CONTENT_LENGTH,0,0,2);goto done;}
            char *end=value+strlen(value);while(end>value&&(end[-1]==' '||end[-1]=='\t'))*--end=0;
            if(end==value){failed(BW_HC_CONTENT_LENGTH,0,0,3);goto done;}
            for(;value<end;value++){unsigned digit=(unsigned)(*value-'0');
                if(*value<'0'||*value>'9'){failed(BW_HC_CONTENT_LENGTH,0,0,4);goto done;}
                if(length>(SIZE_MAX-digit)/10){failed(BW_HC_CONTENT_LENGTH,0,0,5);goto done;}length=length*10+digit;
            }
        }
        line=next;
    }
    if(lengths!=1){failed(BW_HC_CONTENT_LENGTH,0,0,6);goto done;}
    if(types!=1){failed(BW_HC_CONTENT_TYPE,0,0,3);goto done;}
    if(!length||length>BW_HOME_REQUEST_LIMIT){failed(BW_HC_CONTENT_LENGTH,0,0,7);goto done;}
    while(*used<length){ssize_t got=receive(fd,out+*used,length-*used,clock,BW_HC_IO_BODY);
        if(got<=0||(size_t)got>length-*used){failed(BW_HC_BODY,got,0,0);goto done;}*used+=(size_t)got;
    }
    char extra;ssize_t got=receive(fd,&extra,1,clock,BW_HC_IO_EOF);
    if(got!=0){failed(BW_HC_EOF,got,0,0);goto done;}
    if(clock_current(clock,NULL))goto done;
    out[*used]=0;result=0;
done:
    if(close_fd(fd))result=-1;
    if(result){memset(out,0,BW_HOME_REQUEST_LIMIT+1);*used=0;}return result;
}
static int fresh_nonce(char out[33]){
    int mib[2]={CTL_KERN,KERN_ARND};uint8_t bytes[16]={0};size_t size=sizeof bytes;unsigned nonzero=0;
    int result=sysctl(mib,2,bytes,&size,NULL,0);int saved=errno;
    if(result)return failed(BW_HC_NONCE,result,saved,1);
    if(size!=sizeof bytes)return failed(BW_HC_NONCE,(int64_t)size,0,2);
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<sizeof bytes;i++){nonzero|=bytes[i];out[2*i]=digits[bytes[i]>>4];out[2*i+1]=digits[bytes[i]&15];}
    memset(bytes,0,sizeof bytes);out[32]=0;return nonzero?0:failed(BW_HC_NONCE,0,0,3);
}
void bw_home_client_map_sha256(const void *bytes,size_t size,uint8_t digest[32]){cp_sha_bytes(bytes,size,digest);}
int bw_home_client_claim(int32_t pid,const uint8_t map_sha256[32],uint64_t deadline_us,char run[33]){
    memset(&first_failure,0,sizeof first_failure);response_status=0;
    if(!run)return failed(BW_HC_ARGUMENT,0,0,1);memset(run,0,33);uint64_t stamp;
    if(pid<=1)return failed(BW_HC_ARGUMENT,pid,0,2);
    if(!map_sha256)return failed(BW_HC_ARGUMENT,0,0,3);
    if(clock_now(&stamp))return -1;
    if(deadline_us>INT64_MAX)return failed(BW_HC_DEADLINE,0,0,3);
    if(deadline_us<=stamp)return failed(BW_HC_DEADLINE,0,0,2);
    if(deadline_us-stamp>HOME_US)return failed(BW_HC_DEADLINE,0,0,4);
    struct transfer_clock clock={deadline_us,stamp};struct bw_home_claim claim={.pid=pid,.deadline_us=deadline_us};
    memcpy(claim.map_sha256,map_sha256,32);char body[BW_HOME_REQUEST_LIMIT],reply[BW_HOME_REQUEST_LIMIT+1];size_t size=0,used=0;
    int result=-1,parsed;
    if(fresh_nonce(claim.nonce)||clock_current(&clock,NULL))goto done;
    parsed=bw_home_claim_format(&claim,body,sizeof body,&size);if(parsed){failed(BW_HC_CLAIM_FORMAT,parsed,0,0);goto done;}
    if(transfer(body,size,reply,&used,&clock))goto done;
    parsed=bw_home_ack_parse(reply,used,claim.nonce,run);if(parsed){failed(BW_HC_ACK,parsed,0,0);goto done;}
    if(clock_current(&clock,NULL))goto done;
    result=0;
done:
    memset(&claim,0,sizeof claim);memset(body,0,sizeof body);memset(reply,0,sizeof reply);if(result)memset(run,0,33);return result;
}
