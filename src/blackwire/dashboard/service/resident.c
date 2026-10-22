/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "resident.h"
#include "dashboard_service.h"
#include "local_launch.h"
#include "local_removal_runtime.h"
#include "registration_observer.h"
#include "native_setup.h"
#ifndef BW_WEB_ONLY
#include "native_bundle.h"
#endif
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct bd_resident {
    struct bw_native_backend *backend;
    struct bd_local_launcher *local;
    struct bd_local_removal_runtime *removal;
    struct bd_registration_observer *observer;
    struct bd_dashboard_service *service;
    void (*feedback)(const char *);
    pthread_t ticker;
};
static int idle(void *opaque){(void)opaque;return bw_native_setup_idle(NULL);}
static int reserve(void *opaque,const char nonce[33],const char token[33]){
    struct bd_resident *r=opaque;return bw_native_backend_external_reserve(r->backend,nonce,token);
}
static int release(void *opaque,const char nonce[33]){
    struct bd_resident *r=opaque;return bw_native_backend_external_release(r->backend,nonce);
}
static int backend_reserved(void *opaque,const char nonce[33],const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
    struct bd_resident *r=opaque;return bw_native_backend_handle_reserved(r->backend,nonce,method,url,body,size,token,out);
}
static int local_launch(void *opaque,const struct bd_item *item,const char nonce[33]){
    struct bd_resident *r=opaque;return bd_local_launch(r->local,item,nonce);
}
static int local_state(void *opaque,const char nonce[33]){
    struct bd_resident *r=opaque;return bd_local_state(r->local,nonce);
}
static int local_capable(void *opaque,const struct bd_item *item){
    struct bd_resident *r=opaque;return bd_local_launch_capable(r->local,item);
}
static int local_uninstall_capable(void *opaque,const struct bd_item *item){
    struct bd_resident *r=opaque;return bd_local_removal_runtime_capable(r->removal,item);
}
static int local_uninstall(void *opaque,const struct bd_item *item,const char nonce[33],const char title[10]){
    struct bd_resident *r=opaque;return bd_local_removal_runtime_start(r->removal,item,nonce,title);
}
static int local_uninstall_state(void *opaque,const char nonce[33],struct bw_game_uninstall_result *out){
    struct bd_resident *r=opaque;return bd_local_removal_runtime_state(r->removal,nonce,out);
}
static void feedback(void *opaque,const char nonce[33],const char *text){
    struct bd_resident *r=opaque;(void)nonce;if(r->feedback)r->feedback(text);
}
static void *tick(void *opaque){
    struct bd_resident *r=opaque;
    for(;;){(void)bd_service_tick(r->service);(void)bd_registration_observer_tick(r->observer);struct timespec delay={0,250000000};nanosleep(&delay,NULL);}
    return NULL;
}
struct bd_resident *bd_resident_create(struct bw_native_backend *backend){
    return bd_resident_create_with_feedback(backend,NULL);
}
struct bd_resident *bd_resident_create_with_feedback(struct bw_native_backend *backend,void (*notify)(const char *)){
    if(!backend)return NULL;struct bd_resident *r=calloc(1,sizeof *r);if(!r)return NULL;r->backend=backend;r->feedback=notify;
    struct bd_local_launch_config local={0};r->local=bd_local_launcher_create(&local);if(!r->local){free(r);return NULL;}
    struct bd_local_removal_runtime_config removal={.backend=backend};r->removal=bd_local_removal_runtime_create(&removal);
    if(!r->removal){(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    r->observer=bd_registration_observer_create(backend,NULL);
    if(!r->observer){(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    struct bd_service_config config={.backend=backend,.observe_bfp=0,.http_timeout_ms=2000,
        .ops={.opaque=r,.idle=idle,.reserve=reserve,.release=release,.backend_reserved=backend_reserved,
            .local_launch=local_launch,.local_state=local_state,.local_capable=local_capable,.feedback=notify?feedback:NULL,
            .local_uninstall_capable=local_uninstall_capable,.local_uninstall=local_uninstall,.local_uninstall_state=local_uninstall_state}};
#ifdef BW_WEB_ONLY
    config.native_frontend_disabled=1;
#else
    config.frontend_package=bd_embedded_frontend_package;
#endif
    r->service=bd_service_create(&config);if(!r->service){(void)bd_registration_observer_destroy(r->observer);(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    if(bd_service_attach_settings_guard(r->service)){(void)bd_service_destroy(r->service);(void)bd_registration_observer_destroy(r->observer);(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    if(bd_service_attach_preparation_guard(r->service)){(void)bd_service_destroy(r->service);(void)bd_registration_observer_destroy(r->observer);(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    pthread_attr_t attr;if(pthread_attr_init(&attr)){(void)bd_service_destroy(r->service);(void)bd_registration_observer_destroy(r->observer);(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}
    int rc=pthread_attr_setstacksize(&attr,4u*1024u*1024u);if(!rc)rc=pthread_create(&r->ticker,&attr,tick,r);pthread_attr_destroy(&attr);
    if(rc){(void)bd_service_destroy(r->service);(void)bd_registration_observer_destroy(r->observer);(void)bd_local_removal_runtime_destroy(r->removal);(void)bd_local_launcher_destroy(r->local);free(r);return NULL;}return r;
}
int bd_resident_handle(struct bd_resident *r,const char *method,const char *url,const void *body,size_t size,const char *token,struct bw_native_response *out){
    if(!r)return !strncmp(url,"/api/dashboard/",15)?-1:1;
    return bd_service_handle(r->service,method,url,body,size,token,out);
}
