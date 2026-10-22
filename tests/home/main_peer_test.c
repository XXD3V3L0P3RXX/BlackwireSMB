/* Actual HTTP parser/header/accepted-peer route, real host TCP sockets.
 * Only the backend queue is a modeled recording seam. */
#define _DEFAULT_SOURCE 1
#include "main-under-test.h"
#include <assert.h>
static unsigned cases,checks,claims,normal_requests;
static int observed_peer,observed_header,observed_origin;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"case%u line%u %s\n",cases,__LINE__,#x);exit(1);}}while(0)
int bw_native_backend_home_claim(struct bw_native_backend*p,const void*raw,size_t size,int peer,int header,int origin,struct bw_native_response*out){
    C(p==native_backend&&raw&&size==2&&!memcmp(raw,"{}",2));claims++;observed_peer=peer;observed_header=header;observed_origin=origin;
    const char *reply=peer&&header&&!origin?"{\"accepted\":true}":"{\"error\":\"refused\"}";out->status=peer&&header&&!origin?202:403;out->type="application/json";out->size=strlen(reply);out->bytes=malloc(out->size+1);C(out->bytes);memcpy(out->bytes,reply,out->size+1);return 0;
}
int bd_resident_handle(struct bd_resident*p,const char*m,const char*u,const void*b,size_t n,const char*t,struct bw_native_response*out){(void)p;(void)m;(void)u;(void)b;(void)n;(void)t;(void)out;normal_requests++;return 1;}
int bw_native_backend_handle_authenticated(struct bw_native_backend*p,const char*m,const char*u,const void*b,size_t n,const char*t,struct bw_native_response*out){(void)p;(void)m;(void)u;(void)b;(void)n;(void)t;normal_requests++;out->status=404;out->type="application/json";out->bytes=strdup("{}");out->size=2;return 0;}
void bw_native_response_clear(struct bw_native_response*out){free(out->bytes);memset(out,0,sizeof *out);}
void bw_native_sensitive_clear(void*p,size_t n){memset(p,0,n);}
static void exchange(const char *request,int tcp,char response[4096]){
    cases++;int client,server,listener=-1;
    if(tcp){listener=socket(AF_INET,SOCK_STREAM,0);C(listener>=0);struct sockaddr_in a={.sin_family=AF_INET,.sin_port=0,.sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};C(!bind(listener,(struct sockaddr*)&a,sizeof a));C(!listen(listener,1));socklen_t n=sizeof a;C(!getsockname(listener,(struct sockaddr*)&a,&n));client=socket(AF_INET,SOCK_STREAM,0);C(client>=0&&!connect(client,(struct sockaddr*)&a,sizeof a));server=accept(listener,NULL,NULL);C(server>=0);C(!close(listener));}
    else{int pair[2];C(!socketpair(AF_UNIX,SOCK_STREAM,0,pair));client=pair[0];server=pair[1];}
    C(write(client,request,strlen(request))==(ssize_t)strlen(request));C(!shutdown(client,SHUT_WR));serve(server,"127.0.0.1");C(!close(server));size_t at=0;
    for(;;){ssize_t n=read(client,response+at,4095-at);C(n>=0);if(!n)break;at+=(size_t)n;C(at<4095);}response[at]=0;C(!close(client));
}
int main(void){
    native_backend=(struct bw_native_backend*)(uintptr_t)1;dashboard=(struct bd_resident*)(uintptr_t)2;char response[4096],request[2048];
    const char *base="POST /api/reader/bootstrap HTTP/1.1\r\nHost: 127.0.0.1:5918\r\nContent-Type: application/json\r\nX-Blackwire-Reader: home/1\r\nContent-Length: 2\r\n";
    snprintf(request,sizeof request,"%s\r\n{}",base);exchange(request,1,response);C(strstr(response,"HTTP/1.0 202 ")&&claims==1&&observed_peer&&observed_header&&!observed_origin&&!normal_requests);C(!strstr(response,"Access-Control-Allow-Origin"));
    exchange(request,0,response);C(strstr(response," 403 ")&&claims==2&&!observed_peer); /* Host does not certify peer. */
    snprintf(request,sizeof request,"%sOrigin: http://127.0.0.1:5918\r\n\r\n{}",base);exchange(request,1,response);C(strstr(response," 403 ")&&observed_origin);
    snprintf(request,sizeof request,"%sReferer: http://example.invalid/\r\n\r\n{}",base);exchange(request,1,response);C(strstr(response," 403 ")&&observed_origin);
    snprintf(request,sizeof request,"%sX-Blackwire-Reader: home/1\r\n\r\n{}",base);exchange(request,1,response);C(strstr(response," 403 ")&&!observed_header);
    snprintf(request,sizeof request,"%s\r\n{}EXTRA",base);unsigned before=claims;exchange(request,1,response);C(strstr(response," 400 ")&&claims==before);
    snprintf(request,sizeof request,"%sContent-Length: 2\r\n\r\n{}",base);exchange(request,1,response);C(strstr(response," 400 ")&&claims==before);
    exchange("GET /api/reader/bootstrap HTTP/1.1\r\nHost:127.0.0.1\r\n\r\n",1,response);C(strstr(response," 405 ")&&claims==before);
    exchange("POST /api/reader/bootstrap HTTP/1.1\r\nHost:127.0.0.1\r\nContent-Type:text/plain\r\nX-Blackwire-Reader:home/1\r\nContent-Length:2\r\n\r\n{}",1,response);C(strstr(response," 415 ")&&claims==before);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualHttpParserAndTrustedPeerRoute\":true,\"backendQueueModeled\":true}\n",cases,checks);return 0;
}
