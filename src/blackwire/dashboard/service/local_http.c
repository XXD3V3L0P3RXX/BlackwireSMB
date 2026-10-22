/* SPDX-License-Identifier: GPL-3.0-or-later
 * Nonblocking loopback transport follows Blackwire's shadowmount_http.c.
 * This small client adds fixed observation routes and strict bounded framing;
 * it does not reuse that client's registration/mutation routes. */
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include "local_http.h"
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

#define BD_HEADER_LIMIT 8192u
#define BD_SMP_BODY_LIMIT 1048576u
#define BD_BFP_BODY_LIMIT 16384u

struct bd_call {
    const struct bd_http_options *options;
    struct bd_http_reply *reply;
    uint64_t last, deadline;
};

static int fail(struct bd_call *c, const char *reason, int code) {
    if (!c->reply->error) {
        c->reply->error=reason;
        c->reply->sys_errno=code;
    }
    return -1;
}

static int monotonic_ms(uint64_t *value) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC,&t)) return -1;
    if (t.tv_sec<0 || t.tv_nsec<0 || t.tv_nsec>=1000000000L ||
        (uint64_t)t.tv_sec>(UINT64_MAX-999u)/1000u) return -1;
    *value=(uint64_t)t.tv_sec*1000u+(uint64_t)t.tv_nsec/1000000u;
    return 0;
}

static int current(struct bd_call *c) {
    uint64_t at;
    if (monotonic_ms(&at)) return fail(c,"clock",0);
    if (at<c->last) return fail(c,"clock_rewind",0);
    c->last=at;
    if (at>=c->deadline) return fail(c,"deadline",0);
    if (c->options && c->options->current &&
        c->options->current(c->options->opaque)) return fail(c,"cancelled",0);
    /* The caller guard can take time. Its return cannot renew the deadline. */
    if (monotonic_ms(&at)) return fail(c,"clock",0);
    if (at<c->last) return fail(c,"clock_rewind",0);
    c->last=at;
    return at>=c->deadline ? fail(c,"deadline",0) : 0;
}

static int wait_socket(int fd, short events, struct bd_call *c) {
    for (;;) {
        if (current(c)) return -1;
        uint64_t left=c->deadline-c->last;
        int delay=(int)(left>50u ? 50u : left);
        struct pollfd p={.fd=fd,.events=events,.revents=0};
        int rc=poll(&p,1,delay);
        if (rc<0 && errno==EINTR) continue;
        if (rc<0) return fail(c,"poll",errno);
        if (p.revents&POLLNVAL) return fail(c,"poll_invalid",0);
        if (rc>0 && (p.revents&(events|POLLERR|POLLHUP))) return current(c);
    }
}

static int send_all(int fd, const char *bytes, size_t size, struct bd_call *c) {
    size_t used=0;
    while (used<size) {
        if (wait_socket(fd,POLLOUT,c)) return -1;
#ifdef MSG_NOSIGNAL
        ssize_t n=send(fd,bytes+used,size-used,MSG_NOSIGNAL);
#else
        ssize_t n=send(fd,bytes+used,size-used,0);
#endif
        if (n<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
        if (n<0) return fail(c,"send",errno);
        if (!n) return fail(c,"send_zero",0);
        used+=(size_t)n;
        if (current(c)) return -1;
    }
    return 0;
}

static int token(unsigned char c) {
    return (c>='a' && c<='z') || (c>='A' && c<='Z') ||
           (c>='0' && c<='9') || (c && strchr("!#$%&'*+-.^_`|~",(int)c));
}

static int parse_header(const unsigned char *bytes, size_t size, size_t maximum,
                        size_t *body_size, struct bd_call *c) {
    char text[BD_HEADER_LIMIT+1u];
    if (size<16u || size>BD_HEADER_LIMIT || memchr(bytes,0,size))
        return fail(c,"header_invalid",0);
    memcpy(text,bytes,size);text[size]=0;
    char *line=strstr(text,"\r\n");
    if (!line) return fail(c,"status_invalid",0);
    *line=0;
    size_t status_length=strlen(text);
    if (status_length<12u ||
        (strncmp(text,"HTTP/1.0 ",9) && strncmp(text,"HTTP/1.1 ",9)) ||
        text[9]<'1' || text[9]>'5' || text[10]<'0' || text[10]>'9' ||
        text[11]<'0' || text[11]>'9' || (text[12] && text[12]!=' '))
        return fail(c,"status_invalid",0);
    for (const unsigned char *p=(const unsigned char *)text;*p;p++)
        if (*p<32u || *p>=127u) return fail(c,"status_invalid",0);
    c->reply->status=(unsigned)(text[9]-'0')*100u+
                     (unsigned)(text[10]-'0')*10u+(unsigned)(text[11]-'0');
    if (c->reply->status<200u) return fail(c,"informational_refused",0);
    if (c->reply->status>=300u && c->reply->status<400u)
        return fail(c,"redirect_refused",0);
    unsigned length_seen=0;
    size_t length=0;
    char *at=line+2;
    for (;;) {
        line=strstr(at,"\r\n");
        if (!line) return fail(c,"header_invalid",0);
        *line=0;
        if (!*at) {
            if (line+2!=text+size) return fail(c,"header_invalid",0);
            break;
        }
        if (*at==' ' || *at=='\t') return fail(c,"header_folded",0);
        char *colon=strchr(at,':');
        if (!colon || colon==at) return fail(c,"header_invalid",0);
        for (char *p=at;p<colon;p++)
            if (!token((unsigned char)*p)) return fail(c,"header_invalid",0);
        *colon++=0;
        while (*colon==' ' || *colon=='\t') colon++;
        char *end=colon+strlen(colon);
        while (end>colon && (end[-1]==' ' || end[-1]=='\t')) *--end=0;
        for (char *p=colon;p<end;p++)
            if (((unsigned char)*p<32u && *p!='\t') || (unsigned char)*p>=127u)
                return fail(c,"header_invalid",0);
        if (!strcasecmp(at,"Transfer-Encoding"))
            return fail(c,"transfer_encoding_refused",0);
        if (!strcasecmp(at,"Content-Length")) {
            if (length_seen++ || !*colon) return fail(c,"length_invalid",0);
            for (char *p=colon;*p;p++) {
                unsigned digit=(unsigned char)*p-(unsigned)'0';
                if (digit>9u) return fail(c,"length_invalid",0);
                if (length>(maximum-digit)/10u) return fail(c,"body_limit",0);
                length=length*10u+digit;
            }
            if (length>maximum) return fail(c,"body_limit",0);
        }
        at=line+2;
    }
    if (length_seen!=1u) return fail(c,"length_required",0);
    *body_size=length;
    return 0;
}

void bd_local_http_reply_destroy(struct bd_http_reply *reply) {
    if (!reply) return;
    free(reply->bytes);
    memset(reply,0,sizeof *reply);
}

int bd_local_http(enum bd_http_route route, const struct bd_http_options *options,
                  struct bd_http_reply *reply) {
    if (!reply) return -1;
    memset(reply,0,sizeof *reply);
    struct bd_call c={.options=options,.reply=reply,.last=0,.deadline=0};
    if (route!=BD_HTTP_SMP_GAMES && route!=BD_HTTP_BFP_JOB_STATUS)
        return fail(&c,"route_invalid",0);
    unsigned timeout=options && options->timeout_ms ? options->timeout_ms : 10000u;
    if (timeout>30000u) return fail(&c,"timeout_invalid",0);
    if (monotonic_ms(&c.last) || c.last>UINT64_MAX-timeout)
        return fail(&c,"clock",0);
    c.deadline=c.last+timeout;
    if (current(&c)) return -1;
    unsigned port=options && options->port ? options->port :
                  (route==BD_HTTP_SMP_GAMES ? 10101u : 5905u);
    const char *body=route==BD_HTTP_SMP_GAMES ? "{\"include_size\":false}" : "";
    const char *method=route==BD_HTTP_SMP_GAMES ? "POST" : "GET";
    const char *path=route==BD_HTTP_SMP_GAMES ? "/api/v1/games" : "/api/fs/job/status";
    char request[320];
    int request_size=snprintf(request,sizeof request,
        "%s %s HTTP/1.0\r\nHost: 127.0.0.1:%u\r\n"
        "Content-Type: application/json\r\nContent-Length: %zu\r\n"
        "Connection: close\r\n\r\n%s",method,path,port,strlen(body),body);
    if (request_size<0 || (size_t)request_size>=sizeof request)
        return fail(&c,"request_size",0);
    int fd=socket(AF_INET,SOCK_STREAM,0),result=-1;
    unsigned char *response=NULL;
    if (fd<0) return fail(&c,"socket",errno);
    int flags=fcntl(fd,F_GETFL,0);
    if (flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK) ||
        fcntl(fd,F_SETFD,FD_CLOEXEC)) { fail(&c,"socket_flags",errno);goto done; }
#ifdef SO_NOSIGPIPE
    int yes=1;
    if (setsockopt(fd,SOL_SOCKET,SO_NOSIGPIPE,&yes,sizeof yes)) {
        fail(&c,"socket_nosigpipe",errno);goto done;
    }
#endif
    struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons((uint16_t)port),
                               .sin_addr={.s_addr=inet_addr("127.0.0.1")}};
    int connected=connect(fd,(struct sockaddr *)&address,sizeof address);
    if (connected<0 && errno!=EINPROGRESS) { fail(&c,"connect",errno);goto done; }
    if (connected<0) {
        int error=0;socklen_t length=sizeof error;
        if (wait_socket(fd,POLLOUT,&c)) goto done;
        if (getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)) {
            fail(&c,"connect_status",errno);goto done;
        }
        if (length!=sizeof error || error) {
            fail(&c,"connect",error);goto done;
        }
    }
    if (current(&c) || send_all(fd,request,(size_t)request_size,&c)) goto done;
    size_t maximum=route==BD_HTTP_SMP_GAMES ? BD_SMP_BODY_LIMIT : BD_BFP_BODY_LIMIT;
    size_t capacity=BD_HEADER_LIMIT+maximum+1u,used=0,head=0,body_size=0;
    response=malloc(capacity);
    if (!response) { fail(&c,"allocate",errno);goto done; }
    for (;;) {
        if (wait_socket(fd,POLLIN,&c)) goto done;
        ssize_t got=recv(fd,response+used,capacity-used,0);
        if (got<0 && (errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)) continue;
        if (got<0) { fail(&c,"receive",errno);goto done; }
        if (!got) break;
        used+=(size_t)got;
        if (current(&c)) goto done;
        if (!head) {
            for (size_t i=3;i<used && i<BD_HEADER_LIMIT;i++)
                if (!memcmp(response+i-3,"\r\n\r\n",4)) { head=i+1u;break; }
            if (head) {
                if (parse_header(response,head,maximum,&body_size,&c)) goto done;
            } else if (used>=BD_HEADER_LIMIT) {
                fail(&c,"header_limit",0);goto done;
            }
        }
        if ((head && used>head+body_size) || used==capacity) {
            fail(&c,"body_overrun",0);goto done;
        }
    }
    if (!head || used!=head+body_size) { fail(&c,"truncated",0);goto done; }
    if (current(&c)) goto done;
    memmove(response,response+head,body_size);
    response[body_size]=0;
    reply->bytes=response;response=NULL;reply->size=body_size;result=0;
done:
    free(response);
    if (close(fd)) { fail(&c,"close",errno);result=-1; }
    if (result) { free(reply->bytes);reply->bytes=NULL;reply->size=0; }
    return result;
}
