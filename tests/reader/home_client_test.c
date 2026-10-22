/* Actual client/codec; all kernel, clock and socket calls are modeled. */
#define _DEFAULT_SOURCE 1
#include "reader/preload/home_client.h"
#include "library/home_protocol.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
enum {GOOD10,GOOD11,BAD_STATUS,DUP_LENGTH,CHUNKED,NO_LENGTH,SHORT_BODY,EXTRA_BODY,NO_EOF,
      HUGE_LENGTH,BAD_TYPE,DUP_TYPE,NUL_HEADER,BAD_HEADER,OVERFLOW_LENGTH,EARLY_EOF,CLOSE_ERROR,
      CONNECT_REFUSED,CONNECT_PENDING,CONNECT_ERROR,CONNECT_SIZE,NOSIG_ERROR,FCNTL_ERROR,SOCKET_ERROR,
      SHORT_SEND,ZERO_SEND,SEND_EINTR,POLL_EINTR,POLL_TIMEOUT,CLOCK_REVERSE,CLOCK_ERROR,RNG_ERROR,RNG_SHORT,
      RNG_ZERO,WRONG_NONCE,DUP_ACK,ZERO_RUN,EXTRA_ACK,SLOW_READ,LONG_HEADER,
      FCNTL_SET_ERROR,CONNECT_RESULT_ERROR,POLL_ERROR,POLL_NVAL,POLL_NO_EVENTS,
      SEND_HEADER_ERROR,SEND_BODY_ERROR,RECV_HEADER_ERROR,RECV_BODY_ERROR,RECV_EOF_ERROR,
      REFUSED_AND_CLOSE_ERROR,STATUS403,STATUS409,STATUS503,TYPE_CHARSET,CLOCK_INVALID,
      NBIO_ERROR,NBIO_CLOSE_ERROR,NBIO_INTERRUPTED,NBIO_UNSUPPORTED,LAST_CASE};
_Static_assert(BW_HC_FCNTL_SET==7&&BW_HC_ACK==23&&BW_HC_NONBLOCK==24,"Previous diagnostic stages must not move");
static unsigned mode,checks,cases,sockets,connects,closes,sends,receives,polls,clocks,rngs,nosigs;
static unsigned nbios,fcntls,ioctls;
static uint64_t tick;
static uint64_t behavior=UINT64_C(1469598103934665603);
static void fold(uint64_t value){behavior=(behavior^value)*UINT64_C(1099511628211);}
static char wire[2048],response[4096];static size_t wire_size,response_size,response_at;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"home-client mode%u line%d: %s\n",mode,__LINE__,#x);exit(1);}}while(0)
static void response_prepare(void){
    char *body=strstr(wire,"\r\n\r\n");CHECK(body);body+=4;
    CHECK(!strncmp(wire,"POST /api/reader/bootstrap HTTP/1.1\r\n",37));
    CHECK(strstr(wire,"Host: 127.0.0.1:5918\r\n")&&strstr(wire,"X-Blackwire-Reader: home/1\r\n"));
    CHECK(!strstr(wire,"Origin:")&&!strstr(wire,"Referer:")&&!strstr(wire,"password"));
    struct bw_home_claim claim={0};CHECK(!bw_home_claim_parse(&claim,body,wire_size-(size_t)(body-wire)));
    CHECK(claim.pid==234&&claim.deadline_us==21000000u);CHECK(claim.map_sha256[0]==0x42);
    char ack[512];size_t used=0;
    if(mode==WRONG_NONCE)claim.nonce[0]=claim.nonce[0]=='a'?'b':'a';
    CHECK(!bw_home_ack_format(claim.nonce,"1234567890abcdef1234567890abcdef",ack,sizeof ack,&used));
    if(mode==ZERO_RUN){char *at=strstr(ack,"\"run\":\"")+7;memset(at,'0',32);}
    if(mode==DUP_ACK||mode==EXTRA_ACK){ack[--used]=0;used+=(size_t)snprintf(ack+used,sizeof ack-used,mode==DUP_ACK?",\"accepted\":true}":",\"extra\":true}");}
    char headers[512]={0};
    if(mode!=NO_LENGTH)snprintf(headers,sizeof headers,"Content-Length: %zu\r\n",mode==HUGE_LENGTH?513u:used);
    if(mode==DUP_LENGTH)strcat(headers,"Content-Length: 1\r\n");
    if(mode==CHUNKED)strcat(headers,"Transfer-Encoding: chunked\r\n");
    if(mode==OVERFLOW_LENGTH)strcpy(headers,"Content-Length: 184467440737095516160\r\n");
    if(mode==BAD_HEADER)strcat(headers," folded: bad\r\n");
    if(mode==DUP_TYPE)strcat(headers,"Content-Type: application/json\r\n");
    int n=snprintf(response,sizeof response,"HTTP/1.%d %d Accepted\r\nContent-Type: %s\r\n%s\r\n%s",
        mode==GOOD11?1:0,mode==BAD_STATUS?200:mode==STATUS403?403:mode==STATUS409?409:mode==STATUS503?503:202,
        mode==BAD_TYPE?"text/plain":mode==TYPE_CHARSET?"application/json; charset=utf-8":"application/json",headers,ack);
    CHECK(n>0&&(size_t)n<sizeof response);response_size=(size_t)n;
    if(mode==SHORT_BODY)response_size--;
    if(mode==EXTRA_BODY)response[response_size++]='x';
    if(mode==NUL_HEADER)response[15]=0;
    if(mode==EARLY_EOF)response_size=8;
    if(mode==LONG_HEADER){memset(response,'x',sizeof response);response_size=sizeof response;}
}
int __wrap_clock_gettime(clockid_t which,struct timespec *out){
    CHECK(which==CLOCK_MONOTONIC);clocks++;
    if(mode==CLOCK_ERROR&&clocks==5){errno=EIO;return -1;}
    if(mode==CLOCK_REVERSE&&clocks==5)tick-=1000;else tick+=10;
    out->tv_sec=(time_t)(tick/1000000u);out->tv_nsec=(long)(tick%1000000u)*1000;
    if(mode==CLOCK_INVALID&&clocks==5)out->tv_nsec=1000000000L;return 0;
}
int __wrap_sysctl(const int *mib,unsigned count,void *out,size_t *size,const void *in,size_t in_size){
    CHECK(mib&&mib[0]==1&&mib[1]==37&&count==2&&!in&&!in_size&&*size==16);rngs++;
    if(mode==RNG_ERROR){errno=EPERM;return -1;}
    memset(out,mode==RNG_ZERO?0:0xab,16);if(mode==RNG_SHORT)*size=15;return 0;
}
int __wrap_socket(int domain,int type,int protocol){CHECK(domain==AF_INET&&type==SOCK_STREAM&&!protocol);sockets++;if(mode==SOCKET_ERROR){errno=EACCES;return -1;}return 19;}
int __wrap_fcntl(int fd,int command,...){CHECK(fd==19);fcntls++;if(command==F_SETFL||mode==FCNTL_ERROR){errno=EACCES;return -1;}return 0;}
int __wrap_ioctl(int fd,unsigned long request,...){(void)fd;(void)request;ioctls++;errno=EACCES;return -1;}
int __wrap_setsockopt(int fd,int level,int option,const void *value,socklen_t size){
    if(option==0x1200){CHECK(fd==19&&level==SOL_SOCKET&&size==sizeof(int)&&*(const int*)value==1);nbios++;CHECK(nbios==1);
        if(mode==NBIO_ERROR||mode==NBIO_CLOSE_ERROR){errno=EACCES;return -1;}
        if(mode==NBIO_INTERRUPTED){errno=EINTR;return -1;}
        if(mode==NBIO_UNSUPPORTED){errno=ENOPROTOOPT;return -1;}return 0;
    }
    CHECK(fd==19&&level==SOL_SOCKET&&option==0x1022&&size==sizeof(int)&&*(const int*)value==1);nosigs++;if(mode==NOSIG_ERROR){errno=ENOPROTOOPT;return -1;}return 0;
}
int __wrap_connect(int fd,const struct sockaddr *raw,socklen_t size){
    const struct sockaddr_in *address=(const struct sockaddr_in*)raw;
    CHECK(nbios==1);
    CHECK(fd==19&&size==sizeof *address&&address->sin_family==AF_INET&&address->sin_port==htons(5918)&&address->sin_addr.s_addr==htonl(INADDR_LOOPBACK));
    connects++;CHECK(connects==1);if(mode==CONNECT_REFUSED||mode==REFUSED_AND_CLOSE_ERROR){errno=ECONNREFUSED;return -1;}
    if(mode==CONNECT_PENDING||mode==CONNECT_ERROR||mode==CONNECT_SIZE||mode==CONNECT_RESULT_ERROR){errno=EINPROGRESS;return -1;}return 0;
}
int __wrap_getsockopt(int fd,int level,int option,void *out,socklen_t *size){
    CHECK(fd==19&&level==SOL_SOCKET&&option==SO_ERROR&&*size==sizeof(int));if(mode==CONNECT_RESULT_ERROR){errno=EIO;return -1;}*(int*)out=mode==CONNECT_ERROR?ECONNREFUSED:0;if(mode==CONNECT_SIZE)*size=1;return 0;
}
int __wrap_poll(struct pollfd *p,nfds_t size,int timeout){
    CHECK(size==1&&p->fd==19&&timeout>0&&timeout<=1000);polls++;
    if(mode==POLL_EINTR&&polls==1){errno=EINTR;return -1;}
    if(mode==POLL_ERROR){errno=EIO;return -1;}
    if(mode==POLL_NVAL){p->revents=POLLNVAL;return 1;}
    if(mode==POLL_NO_EVENTS){p->revents=0;return 1;}
    if(mode==POLL_TIMEOUT){uint64_t left=tick<21000000u?21000000u-tick:0;tick+=left<1000000u?left:1000000u;return 0;}
    p->revents=p->events;return 1;
}
ssize_t __wrap_send(int fd,const void *data,size_t size,int flags){
    CHECK(fd==19&&(flags&MSG_NOSIGNAL));sends++;
    if(mode==SEND_EINTR&&sends==1){errno=EINTR;return -1;}
    if((mode==SEND_HEADER_ERROR&&sends==1)||(mode==SEND_BODY_ERROR&&sends==2)){errno=EPIPE;return -1;}
    if(mode==ZERO_SEND)return 0;
    size_t take=mode==SHORT_SEND&&size>7?7:size;CHECK(wire_size+take<sizeof wire);
    memcpy(wire+wire_size,data,take);wire_size+=take;wire[wire_size]=0;return (ssize_t)take;
}
ssize_t __wrap_recv(int fd,void *data,size_t size,int flags){
    CHECK(fd==19&&!flags&&size);receives++;
    if(mode==RECV_HEADER_ERROR){errno=EIO;return -1;}
    if(!response_size)response_prepare();
    if(mode==RECV_BODY_ERROR&&response_at>=(size_t)(strstr(response,"\r\n\r\n")+4-response)){errno=EIO;return -1;}
    if(mode==RECV_EOF_ERROR&&response_at==response_size){errno=ECONNRESET;return -1;}
    if(response_at==response_size){if(mode==NO_EOF){uint64_t left=tick<21000000u?21000000u-tick:0;tick+=left<1000000u?left:1000000u;errno=EAGAIN;return -1;}return 0;}
    size_t take=response_size-response_at;if(take>size)take=size;if(mode==SLOW_READ&&take>3)take=3;
    memcpy(data,response+response_at,take);response_at+=take;return (ssize_t)take;
}
int __wrap_close(int fd){CHECK(fd==19);closes++;CHECK(closes==1);if(mode==CLOSE_ERROR||mode==REFUSED_AND_CLOSE_ERROR||mode==NBIO_CLOSE_ERROR){errno=EIO;return -1;}return 0;}
static void reset(void){sockets=connects=closes=sends=receives=polls=clocks=rngs=nosigs=nbios=fcntls=ioctls=0;tick=1000000u;wire_size=response_size=response_at=0;memset(wire,0,sizeof wire);memset(response,0,sizeof response);errno=0;}
#ifndef BW_HOME_DIAGNOSTIC_BASELINE
static void diagnostic(int success){
    struct bw_home_client_failure f={0},again={0};bw_home_client_diagnostic(&f);
    if(success){CHECK(!f.stage&&!f.raw&&!f.error&&!f.detail&&!f.http_status);return;}
    CHECK(f.stage);errno=E2BIG;bw_home_client_diagnostic(&again);CHECK(errno==E2BIG);
    CHECK(f.stage==again.stage&&f.detail==again.detail&&f.raw==again.raw&&f.error==again.error&&f.http_status==again.http_status);
    unsigned stage=0,detail=0,status=0;int error=0;int64_t raw=0;
    switch(mode){
    case BAD_STATUS:case STATUS403:case STATUS409:case STATUS503:stage=BW_HC_HTTP_STATUS;detail=2;raw=status=mode==BAD_STATUS?200:mode==STATUS403?403:mode==STATUS409?409:503;break;
    case DUP_LENGTH:stage=BW_HC_CONTENT_LENGTH;detail=2;status=202;break;
    case CHUNKED:stage=BW_HC_HTTP_HEADER;detail=10;status=202;break;
    case NO_LENGTH:stage=BW_HC_CONTENT_LENGTH;detail=6;status=202;break;
    case SHORT_BODY:stage=BW_HC_BODY;status=202;break;
    case EXTRA_BODY:stage=BW_HC_EOF;raw=1;status=202;break;
    case NO_EOF:case POLL_TIMEOUT:stage=BW_HC_DEADLINE;detail=2;status=mode==NO_EOF?202:0;break;
    case HUGE_LENGTH:stage=BW_HC_CONTENT_LENGTH;detail=7;status=202;break;
    case BAD_TYPE:case TYPE_CHARSET:stage=BW_HC_CONTENT_TYPE;detail=2;status=202;break;
    case DUP_TYPE:stage=BW_HC_CONTENT_TYPE;detail=1;status=202;break;
    case NUL_HEADER:stage=BW_HC_HTTP_HEADER;detail=2;break;
    case BAD_HEADER:stage=BW_HC_HTTP_HEADER;detail=8;status=202;break;
    case OVERFLOW_LENGTH:stage=BW_HC_CONTENT_LENGTH;detail=5;status=202;break;
    case EARLY_EOF:stage=BW_HC_HTTP_HEADER;detail=1;break;
    case LONG_HEADER:stage=BW_HC_HTTP_HEADER;detail=3;raw=2047;break;
    case CLOSE_ERROR:stage=BW_HC_CLOSE;raw=-1;error=EIO;status=202;break;
    case CONNECT_REFUSED:case REFUSED_AND_CLOSE_ERROR:stage=BW_HC_CONNECT;raw=-1;error=ECONNREFUSED;break;
    case CONNECT_ERROR:stage=BW_HC_CONNECT_RESULT;detail=3;raw=ECONNREFUSED;break;
    case CONNECT_SIZE:stage=BW_HC_CONNECT_RESULT;detail=2;raw=1;break;
    case CONNECT_RESULT_ERROR:stage=BW_HC_CONNECT_RESULT;detail=1;raw=-1;error=EIO;break;
    case NOSIG_ERROR:stage=BW_HC_NOSIGPIPE;raw=-1;error=ENOPROTOOPT;break;
    case FCNTL_ERROR:stage=BW_HC_FCNTL_GET;raw=-1;error=EBADF;break;
    case FCNTL_SET_ERROR:stage=BW_HC_FCNTL_SET;raw=-1;error=EOPNOTSUPP;break;
    case SOCKET_ERROR:stage=BW_HC_SOCKET;raw=-1;error=EACCES;break;
    case ZERO_SEND:stage=BW_HC_SEND;detail=BW_HC_IO_SEND_HEADER;break;
    case SEND_HEADER_ERROR:case SEND_BODY_ERROR:stage=BW_HC_SEND;detail=mode==SEND_HEADER_ERROR?BW_HC_IO_SEND_HEADER:BW_HC_IO_SEND_BODY;raw=-1;error=EPIPE;break;
    case RECV_HEADER_ERROR:case RECV_BODY_ERROR:case RECV_EOF_ERROR:stage=BW_HC_RECEIVE;detail=mode==RECV_HEADER_ERROR?BW_HC_IO_HEADER:mode==RECV_BODY_ERROR?BW_HC_IO_BODY:BW_HC_IO_EOF;raw=-1;error=mode==RECV_EOF_ERROR?ECONNRESET:EIO;status=mode==RECV_HEADER_ERROR?0:202;break;
    case POLL_ERROR:case POLL_NVAL:case POLL_NO_EVENTS:stage=BW_HC_POLL;detail=(BW_HC_IO_SEND_HEADER<<16)|(mode==POLL_NVAL?POLLNVAL:0);raw=mode==POLL_ERROR?-1:1;error=mode==POLL_ERROR?EIO:0;break;
    case CLOCK_REVERSE:stage=BW_HC_DEADLINE;detail=1;break;
    case CLOCK_ERROR:stage=BW_HC_CLOCK;detail=1;raw=-1;error=EIO;break;
    case CLOCK_INVALID:stage=BW_HC_CLOCK;detail=2;break;
    case RNG_ERROR:stage=BW_HC_NONCE;detail=1;raw=-1;error=EPERM;break;
    case RNG_SHORT:stage=BW_HC_NONCE;detail=2;raw=15;break;
    case RNG_ZERO:stage=BW_HC_NONCE;detail=3;break;
    case WRONG_NONCE:case DUP_ACK:case ZERO_RUN:case EXTRA_ACK:stage=BW_HC_ACK;raw=-1;status=202;break;
    case NBIO_ERROR:case NBIO_CLOSE_ERROR:case NBIO_INTERRUPTED:case NBIO_UNSUPPORTED:stage=BW_HC_NONBLOCK;raw=-1;error=mode==NBIO_INTERRUPTED?EINTR:mode==NBIO_UNSUPPORTED?ENOPROTOOPT:EACCES;break;
    default:CHECK(0);
    }
    CHECK(f.stage==stage);CHECK(f.detail==detail);CHECK(f.raw==raw);CHECK(f.error==error);CHECK(f.http_status==status);
}
#endif
int main(void){
    uint8_t hash[32]={0x42};char run[33];
#ifdef BW_HOME_NBIO_BEFORE
    mode=FCNTL_SET_ERROR;reset();int refused=bw_home_client_claim(234,hash,21000000u,run);
    struct bw_home_client_failure before={0};bw_home_client_diagnostic(&before);
    CHECK(refused==-1&&before.stage==BW_HC_FCNTL_SET&&before.raw==-1&&before.error==EACCES);
    CHECK(fcntls==2&&!ioctls&&!nbios&&sockets==1&&!connects&&closes==1);
    printf("{\"passed\":true,\"cases\":1,\"checks\":%u,\"beforeRepair\":true,\"failureStage\":%u,\"failureErrno\":%d,\"connects\":%u}\n",checks,before.stage,before.error,connects);return 0;
#endif
    for(mode=0;mode<LAST_CASE;mode++){
        reset();cases++;memset(run,'x',sizeof run);
        int result=bw_home_client_claim(234,hash,21000000u,run);
        int success=mode==GOOD10||mode==GOOD11||mode==CONNECT_PENDING||mode==SHORT_SEND||mode==SEND_EINTR||mode==POLL_EINTR||mode==SLOW_READ||mode==FCNTL_ERROR||mode==FCNTL_SET_ERROR;
        CHECK((result==0)==success);CHECK(connects<=1&&closes<=1);if(sockets&&mode!=SOCKET_ERROR)CHECK(closes==1);
        CHECK(!fcntls&&!ioctls);CHECK(nbios<=1);
        if(mode==NBIO_ERROR||mode==NBIO_CLOSE_ERROR||mode==NBIO_INTERRUPTED||mode==NBIO_UNSUPPORTED)CHECK(!connects&&!sends&&!receives&&!nosigs&&nbios==1&&closes==1);
        if(success)CHECK(!strcmp(run,"1234567890abcdef1234567890abcdef"));else for(unsigned i=0;i<sizeof run;i++)CHECK(!run[i]);
        CHECK(tick<=21001000u);
        fold(mode);fold((uint64_t)result);fold(sockets);fold(connects);fold(closes);fold(sends);fold(receives);fold(polls);fold(clocks);fold(rngs);fold(tick);fold(wire_size);fold(response_at);
#ifndef BW_HOME_DIAGNOSTIC_BASELINE
        diagnostic(success);
#endif
    }
    mode=GOOD10;reset();cases++;CHECK(bw_home_client_claim(1,hash,21000000u,run)<0&&!sockets&&!rngs);
    reset();cases++;CHECK(bw_home_client_claim(234,hash,1000000u,run)<0&&!sockets&&!rngs);
    reset();cases++;CHECK(bw_home_client_claim(234,hash,41000000u,run)<0&&!sockets&&!rngs);
    reset();cases++;uint8_t actual[32];bw_home_client_map_sha256("abc",3,actual);
    static const uint8_t expected[32]={0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad};CHECK(!memcmp(actual,expected,32));
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"behaviorFingerprint\":\"%016llx\",\"actualClient\":true,\"modeledSockets\":true,\"networkActions\":0}\n",cases,checks,(unsigned long long)behavior);return 0;
}
