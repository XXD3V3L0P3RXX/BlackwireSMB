/* SPDX-License-Identifier: GPL-3.0-or-later
 * Only private linked libsmb2/SDK-arc4 call sites are renamed to these names.
 * No aliases are exported or installed into any game/global symbol table. */
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
int bp_smb_isthreaded=1; /* actual DATA, never a generated function stub */
void bp_smb_srandom(unsigned seed){(void)seed;} /* never seed the game's RNG */
long bp_smb_random(void){return (long)(arc4random()&UINT32_C(0x7fffffff));}
char *bp_smb_getenv(const char *name){(void)name;return NULL;} /* explicit config only */
static int ipv4(const char *s,uint8_t out[4]){
    for(unsigned i=0;i<4;i++){
        unsigned n=0,digits=0;const char *start=s;
        while(*s>='0'&&*s<='9'){if(++digits>3)return -1;n=n*10u+(unsigned)(*s++-'0');}
        if(!digits||n>255||(digits>1&&*start=='0'))return -1;
        out[i]=(uint8_t)n;
        if(i==3)return *s?-1:0;
        if(*s++!='.')return -1;
    }return -1;
}
int bp_smb_getaddrinfo(const char *node,const char *service,const struct addrinfo *hints,struct addrinfo **out){
    if(!out)return EAI_FAIL;
    *out=NULL;uint8_t ip[4];
    if(!node||ipv4(node,ip)||!service||!*service)return EAI_NONAME;
    if(hints&&((hints->ai_family!=AF_UNSPEC&&hints->ai_family!=AF_INET)||
        (hints->ai_socktype&&hints->ai_socktype!=SOCK_STREAM)||
        (hints->ai_protocol&&hints->ai_protocol!=IPPROTO_TCP)||
        (hints->ai_flags&~(AI_NUMERICHOST|AI_NUMERICSERV))))return EAI_FAMILY;
    unsigned port=0;for(const char *p=service;*p;p++){
        if(*p<'0'||*p>'9'||port>6553)return EAI_SERVICE;
        port=port*10u+(unsigned)(*p-'0');if(port>65535)return EAI_SERVICE;
    }
    struct addrinfo *a=calloc(1,sizeof *a+sizeof(struct sockaddr_in));if(!a)return EAI_MEMORY;
    struct sockaddr_in *s=(struct sockaddr_in*)(a+1);
#if defined(__FreeBSD__)
    s->sin_len=sizeof *s;
#endif
    s->sin_family=AF_INET;uint8_t *port_bytes=(uint8_t*)&s->sin_port;port_bytes[0]=(uint8_t)(port>>8);port_bytes[1]=(uint8_t)port;
    memcpy(&s->sin_addr,ip,4);a->ai_family=AF_INET;a->ai_socktype=SOCK_STREAM;a->ai_protocol=IPPROTO_TCP;
    a->ai_addrlen=sizeof *s;a->ai_addr=(struct sockaddr*)s;*out=a;return 0;
}
void bp_smb_freeaddrinfo(struct addrinfo *a){free(a);}
struct protoent *bp_smb_getprotobyname(const char *name){
    static char *aliases[]={NULL};static struct protoent tcp={(char*)"tcp",aliases,IPPROTO_TCP};
    return name&&!strcmp(name,"tcp")?&tcp:NULL;
}
