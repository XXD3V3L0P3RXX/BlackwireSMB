/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "native_removal_request.h"
#include "native_document.h"
#include <string.h>
struct removal_parser {struct bw_native_removal_request *out;unsigned seen,restore;};
static int removal_number(const void *body,size_t size,uint64_t maximum,uint64_t *out){
    const unsigned char *p=body;uint64_t n=0;if(!size||(size>1&&p[0]=='0'))return -1;
    for(size_t i=0;i<size;i++){unsigned d=p[i]-'0';if(d>9||d>maximum||n>(maximum-d)/10)return -1;n=n*10+d;}*out=n;return n?0:-1;
}
static int removal_field(void *opaque,const char *key,size_t kn,const void *body,size_t size){
    struct removal_parser *p=opaque;unsigned bit=0;uint64_t n=0;
    if(kn==4&&!memcmp(key,"name",4))bit=1;
    else if(kn==17&&!memcmp(key,"libraryGeneration",17))bit=2;
    else if(kn==16&&!memcmp(key,"settingsRevision",16))bit=4;
    else if(kn==16&&!memcmp(key,"confirmedTitleId",16)&&!p->restore)bit=8;else return -1;
    if(p->seen&bit)return -1;p->seen|=bit;
    if(bit==1)return bw_document_string(body,size,p->out->selected.source,sizeof p->out->selected.source);
    if(bit==8)return bw_document_string(body,size,p->out->confirmed_title,sizeof p->out->confirmed_title);
    if(removal_number(body,size,bit==2?UINT64_C(9007199254740991):UINT32_MAX,&n))return -1;
    if(bit==2)p->out->selected.library_generation=n;else p->out->selected.settings_revision=(uint32_t)n;return 0;
}
int bw_native_removal_request_parse(const void *body,size_t size,unsigned restore,struct bw_native_removal_request *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!body||!size||size>4096||restore>1)return -1;
    struct removal_parser parser={out,0,restore};int bad=bw_document_fields(body,size,removal_field,&parser)||parser.seen!=(restore?7u:15u)||bw_manifest_validate_path(out->selected.source,strlen(out->selected.source));
    if(!restore){if(strnlen(out->confirmed_title,10)!=9||memcmp(out->confirmed_title,"PPSA",4))bad=1;for(unsigned i=4;i<9;i++)if(out->confirmed_title[i]<'0'||out->confirmed_title[i]>'9')bad=1;}
    if(bad){bw_native_sensitive_clear(out,sizeof *out);return -1;}return 0;
}
