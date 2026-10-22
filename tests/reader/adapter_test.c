#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int bp_smb_getaddrinfo(const char*,const char*,const struct addrinfo*,struct addrinfo**);
void bp_smb_freeaddrinfo(struct addrinfo*);
void bp_smb_srandom(unsigned);
char *bp_smb_getenv(const char*);
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"adapter failed line%d\n",__LINE__);return 1;}}while(0)
int main(void){
    struct addrinfo *a=NULL;CHECK(!bp_smb_getaddrinfo("192.0.2.1","445",NULL,&a));CHECK(a&&a->ai_next==NULL&&a->ai_family==AF_INET&&a->ai_socktype==SOCK_STREAM);
    struct sockaddr_in *s=(void*)a->ai_addr;const unsigned char *p=(const void*)&s->sin_port,*ip=(const void*)&s->sin_addr;
    CHECK(p[0]==1&&p[1]==189&&ip[0]==192&&ip[1]==0&&ip[2]==2&&ip[3]==1);bp_smb_freeaddrinfo(a);
    const char *bad[]={"example.invalid","::1","127.0.0","256.0.0.1","01.2.3.4","1.2.3.4x","1..2.3","1.2.3.4.5",""};
    for(unsigned i=0;i<sizeof bad/sizeof *bad;i++){a=(void*)1;CHECK(bp_smb_getaddrinfo(bad[i],"445",NULL,&a)!=0&&a==NULL);}
    CHECK(bp_smb_getaddrinfo("192.0.2.1","65536",NULL,&a)!=0);CHECK(bp_smb_getaddrinfo("192.0.2.1","smb",NULL,&a)!=0);
    struct addrinfo hints={0};hints.ai_family=AF_INET6;CHECK(bp_smb_getaddrinfo("192.0.2.1","445",&hints,&a)!=0);
    CHECK(!setenv("NTLM_USER_FILE","neutral-ambient-sentinel",1));CHECK(bp_smb_getenv("NTLM_USER_FILE")==NULL);CHECK(!strcmp(getenv("NTLM_USER_FILE"),"neutral-ambient-sentinel"));
    srandom(12345);long expected=random();srandom(12345);bp_smb_srandom(77);CHECK(random()==expected);
    printf("{\"passed\":true,\"checks\":%u,\"networkCalls\":0,\"privateAdapterOnly\":true,\"gameRngNotReseeded\":true}\n",checks);return 0;
}
