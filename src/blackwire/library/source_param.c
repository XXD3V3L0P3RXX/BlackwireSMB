#include "source_param.h"
#include "native_document.h"
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "registration_param_compat.h"
/* Registration-only uniqueness workspace; existing metadata/document parsing
 * leaves keys NULL and retains its frozen behavior. Per-document heap bound:
 * 64 KiB decoded keys, 8192 key records and 16384 open-addressing slots.
 * Slots use a hash to select candidates, then compare complete decoded bytes;
 * a hash collision never establishes key identity. No recursive allocations.
 */
struct registration_key {uint32_t scope,offset,length;};
struct registration_keys {
    unsigned char arena[BW_SOURCE_PARAM_LIMIT+1];
    struct registration_key rows[8192];
    uint32_t slots[16384];
    size_t used;
    unsigned count;
};
static int registration_key_add(struct registration_keys *keys,unsigned scope,size_t length){
    if(length>=sizeof keys->arena-keys->used||keys->count>=8192)return -1;
    uint32_t hash=UINT32_C(2166136261)^(uint32_t)scope;
    for(size_t i=0;i<length;i++)hash=(hash^keys->arena[keys->used+i])*UINT32_C(16777619);
    unsigned at=hash&16383u;
    for(unsigned probes=0;probes<16384;probes++,at=(at+1)&16383u){
        unsigned slot=keys->slots[at];
        if(!slot){
            struct registration_key *key=keys->rows+keys->count;
            key->scope=scope;key->offset=(uint32_t)keys->used;key->length=(uint32_t)length;
            keys->slots[at]=++keys->count;keys->used+=length+1;return 0;
        }
        const struct registration_key *key=keys->rows+slot-1;
        if(key->scope==scope&&key->length==length&&!memcmp(keys->arena+key->offset,keys->arena+keys->used,length))return -1;
    }
    return -1;
}

struct json {const uint8_t *data;size_t size,at;unsigned tokens;char title[10];int found;char *display;size_t display_capacity;char *content;unsigned content_found;int document;bw_document_field_fn visit;void *opaque;struct registration_keys *keys;size_t drm_at,drm_size;};
static void space(struct json *p){while(p->at<p->size&&(p->data[p->at]==' '||p->data[p->at]=='\t'||p->data[p->at]=='\r'||p->data[p->at]=='\n'))p->at++;}
static int hex4(struct json *p,uint32_t *out){
    if(p->size-p->at<4)return -1;
    uint32_t n=0;
    for(unsigned i=0;i<4;i++){unsigned c=p->data[p->at++],v;if(c>='0'&&c<='9')v=c-'0';else if(c>='a'&&c<='f')v=c-'a'+10;else if(c>='A'&&c<='F')v=c-'A'+10;else return -1;n=(n<<4)|v;}
    *out=n;return 0;
}
static int append(char *out,size_t capacity,size_t *used,uint32_t c){
    uint8_t bytes[4];size_t n;
    if(c<0x80){bytes[0]=(uint8_t)c;n=1;}
    else if(c<0x800){bytes[0]=(uint8_t)(0xc0|(c>>6));bytes[1]=(uint8_t)(0x80|(c&63));n=2;}
    else if(c<0x10000){bytes[0]=(uint8_t)(0xe0|(c>>12));bytes[1]=(uint8_t)(0x80|((c>>6)&63));bytes[2]=(uint8_t)(0x80|(c&63));n=3;}
    else {bytes[0]=(uint8_t)(0xf0|(c>>18));bytes[1]=(uint8_t)(0x80|((c>>12)&63));bytes[2]=(uint8_t)(0x80|((c>>6)&63));bytes[3]=(uint8_t)(0x80|(c&63));n=4;}
    if(out&&*used+n<capacity)memcpy(out+*used,bytes,n);
    *used+=n;return 0;
}
static int string(struct json *p,char *out,size_t capacity,size_t *length){
    if(p->at>=p->size||p->data[p->at++]!='"'||++p->tokens>16384)return -1;
    size_t used=0;
    while(p->at<p->size){
        uint32_t c=p->data[p->at++];
        if(c=='"'){if(out&&capacity)out[used<capacity?used:capacity-1]=0;*length=used;return 0;}
        if(c<32)return -1;
        if(c=='\\'){
            if(p->at>=p->size)return -1;
            c=p->data[p->at++];
            if(c=='u'){
                if(hex4(p,&c))return -1;
                if(c>=0xd800&&c<=0xdbff){
                    if(p->size-p->at<6||p->data[p->at]!='\\'||p->data[p->at+1]!='u')return -1;
                    p->at+=2;uint32_t low;if(hex4(p,&low)||low<0xdc00||low>0xdfff)return -1;c=0x10000+((c-0xd800)<<10)+(low-0xdc00);
                }else if(c>=0xdc00&&c<=0xdfff)return -1;
            }else if(c=='b')c=8;else if(c=='f')c=12;else if(c=='n')c=10;else if(c=='r')c=13;else if(c=='t')c=9;
            else if(c!='"'&&c!='\\'&&c!='/')return -1;
        }else if(c>=128){
            unsigned count;uint32_t minimum;
            if(c>=0xc2&&c<=0xdf){count=1;minimum=0x80;c&=31;}
            else if(c>=0xe0&&c<=0xef){count=2;minimum=0x800;c&=15;}
            else if(c>=0xf0&&c<=0xf4){count=3;minimum=0x10000;c&=7;}else return -1;
            if(p->size-p->at<count)return -1;
            for(unsigned i=0;i<count;i++){unsigned next=p->data[p->at++];if((next&0xc0)!=0x80)return -1;c=(c<<6)|(next&63);}
            if(c<minimum||c>0x10ffff||(c>=0xd800&&c<=0xdfff))return -1;
        }
        append(out,capacity,&used,c);
    }
    return -1;
}
static int value(struct json *,unsigned);
static int object(struct json *p,unsigned depth){
    unsigned scope=(unsigned)p->at;
    if(depth>32||p->at>=p->size||p->data[p->at++]!='{')return -1;
    space(p);if(p->at<p->size&&p->data[p->at]=='}'){p->at++;return 0;}
    for(;;){
        char key[128]={0};size_t n;
        if(p->keys){
            size_t at=p->keys->used,capacity=sizeof p->keys->arena-at;
            if(string(p,(char *)p->keys->arena+at,capacity,&n)||n>=capacity||registration_key_add(p->keys,scope,n))return -1;
            memcpy(key,p->keys->arena+at,n<sizeof key-1?n:sizeof key-1);
        }else if(string(p,key,sizeof key,&n))return -1;
        space(p);if(p->at>=p->size||p->data[p->at++]!=':')return -1;space(p);
        size_t key_size=n,start=p->at;
        if(!p->document&&depth==0&&n==7&&!memcmp(key,"titleId",7)){
            if(p->found||string(p,p->title,sizeof p->title,&n)||n!=9)return -1;
            if(memcmp(p->title,"PPSA",4))return -1;
            for(unsigned i=4;i<9;i++)if(p->title[i]<'0'||p->title[i]>'9')return -1;
            p->found=1;
        }else if(p->content&&depth==0&&n==9&&!memcmp(key,"contentId",9)){
            if(p->content_found++||string(p,p->content,128,&n)||!n||n>=128||memchr(p->content,0,n))return -1;
            for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)p->content[i];
                if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return -1;}
        }else if(p->display&&depth==0&&n==9&&!memcmp(key,"titleName",9)){
            p->display[0]=0;
            if(p->at<p->size&&p->data[p->at]=='"'){
                if(++p->tokens>16384||string(p,p->display,p->display_capacity,&n))return -1;
                if(n>=p->display_capacity||memchr(p->display,0,n))p->display[0]=0;
                else {size_t first=0,last=n;while(first<last&&(p->display[first]==' '||p->display[first]=='\t'||p->display[first]=='\r'||p->display[first]=='\n'))first++;
                    while(last>first&&(p->display[last-1]==' '||p->display[last-1]=='\t'||p->display[last-1]=='\r'||p->display[last-1]=='\n'))last--;
                    memmove(p->display,p->display+first,last-first);p->display[last-first]=0;}
            }else if(value(p,depth+1))return -1;
        }else if(value(p,depth+1))return -1;
        if(p->keys&&depth==0&&key_size==18&&!memcmp(key,"applicationDrmType",18)){p->drm_at=start;p->drm_size=p->at-start;}
        if(p->visit&&depth==0&&p->visit(p->opaque,key,key_size,p->data+start,p->at-start))return -1;
        space(p);if(p->at>=p->size)return -1;
        unsigned c=p->data[p->at++];if(c=='}')return 0;if(c!=',')return -1;space(p);
    }
}
static int number(struct json *p){
    if(p->data[p->at]=='-')p->at++;
    if(p->at>=p->size)return -1;
    if(p->data[p->at]=='0')p->at++;
    else {if(p->data[p->at]<'1'||p->data[p->at]>'9')return -1;while(p->at<p->size&&p->data[p->at]>='0'&&p->data[p->at]<='9')p->at++;}
    if(p->at<p->size&&p->data[p->at]=='.'){p->at++;size_t start=p->at;while(p->at<p->size&&p->data[p->at]>='0'&&p->data[p->at]<='9')p->at++;if(p->at==start)return -1;}
    if(p->at<p->size&&(p->data[p->at]=='e'||p->data[p->at]=='E')){
        p->at++;if(p->at<p->size&&(p->data[p->at]=='+'||p->data[p->at]=='-'))p->at++;
        size_t start=p->at;while(p->at<p->size&&p->data[p->at]>='0'&&p->data[p->at]<='9')p->at++;if(p->at==start)return -1;
    }
    return 0;
}
static int value(struct json *p,unsigned depth){
    if(depth>32||p->at>=p->size||++p->tokens>16384)return -1;
    unsigned c=p->data[p->at];size_t n;
    if(c=='{')return object(p,depth);
    if(c=='"')return string(p,NULL,0,&n);
    if(c=='['){p->at++;space(p);if(p->at<p->size&&p->data[p->at]==']'){p->at++;return 0;}
        for(;;){if(value(p,depth+1))return -1;space(p);if(p->at>=p->size)return -1;c=p->data[p->at++];if(c==']')return 0;if(c!=',')return -1;space(p);}}
    const char *word=c=='t'?"true":c=='f'?"false":c=='n'?"null":NULL;
    if(word){n=strlen(word);if(n>p->size-p->at||memcmp(p->data+p->at,word,n))return -1;p->at+=n;return 0;}
    return number(p);
}
static int parse(const void *bytes,size_t size,char title[10],char *display,size_t capacity,char *content){
    if(!title)return -1;
    memset(title,0,10);
    if(content)memset(content,0,128);
    if(display){if(!capacity||capacity>4096)return -1;memset(display,0,capacity);}
    if(!bytes||!size||size>BW_SOURCE_PARAM_LIMIT||memchr(bytes,0,size))return -1;
    struct json p={0};p.data=bytes;p.size=size;p.display=display;p.display_capacity=capacity;p.content=content;
    if(size>=3&&!memcmp(bytes,"\xef\xbb\xbf",3))p.at=3;
    space(&p);if(object(&p,0))return -1;space(&p);
    if(p.at!=size||!p.found||(content&&p.content_found!=1))return -1;
    memcpy(title,p.title,10);return 0;
}
int bw_source_param_title(const void *bytes,size_t size,char title[10]){return parse(bytes,size,title,NULL,0,NULL);}
struct display_value {const void *bytes;size_t size;unsigned count;};
struct localized_display {char *out;size_t capacity;char language[128];unsigned languages,preferred_matches;};
static int localized_field(void *opaque,const char *key,size_t length,const void *bytes,size_t size){
    struct display_value *p=opaque;
    if(length==19&&!memcmp(key,"localizedParameters",19)){p->bytes=bytes;p->size=size;p->count++;}
    return 0;
}
static int language_field(void *opaque,const char *key,size_t length,const void *bytes,size_t size){
    struct localized_display *p=opaque;
    if(length==15&&!memcmp(key,"defaultLanguage",15)){
        p->languages++;
        if(bw_document_string(bytes,size,p->language,sizeof p->language))p->language[0]=0;
    }
    return 0;
}
static int display_field(void *opaque,const char *key,size_t length,const void *bytes,size_t size){
    struct display_value *p=opaque;
    if(length==9&&!memcmp(key,"titleName",9)){p->bytes=bytes;p->size=size;p->count++;}
    return 0;
}
static int locale_field(void *opaque,const char *key,size_t length,const void *bytes,size_t size){
    struct localized_display *p=opaque;
    if(!length||length>=sizeof p->language||memchr(key,0,length)||
       (length==15&&!memcmp(key,"defaultLanguage",15)))return 0;
    int preferred=*p->language&&strlen(p->language)==length&&!memcmp(p->language,key,length);
    if(preferred)p->preferred_matches++;
    if(*p->out&&!preferred)return 0;
    struct display_value name={0};char decoded[4096];
    if(bw_document_fields(bytes,size,display_field,&name)||name.count!=1||
       bw_document_string(name.bytes,name.size,decoded,p->capacity))return 0;
    size_t first=0,last=strlen(decoded);
    while(first<last&&(decoded[first]==' '||decoded[first]=='\t'||decoded[first]=='\r'||decoded[first]=='\n'))first++;
    while(last>first&&(decoded[last-1]==' '||decoded[last-1]=='\t'||decoded[last-1]=='\r'||decoded[last-1]=='\n'))last--;
    if(last>first){memcpy(p->out,decoded+first,last-first);p->out[last-first]=0;}
    return 0;
}
static void localized_title(const void *bytes,size_t size,char *out,size_t capacity){
    /* Display-only fallback over an already validated parameter document.
     * Root titleName wins; otherwise prefer the declared locale, then the first
     * usable localized title. No locale name or game identity is assumed. */
    const uint8_t *data=bytes;
    if(size>=3&&!memcmp(data,"\xef\xbb\xbf",3)){data+=3;size-=3;}
    struct display_value localized={0};struct localized_display p={.out=out,.capacity=capacity};
    if(bw_document_fields(data,size,localized_field,&localized)||localized.count!=1||
       bw_document_fields(localized.bytes,localized.size,language_field,&p)||p.languages>1)return;
    if(bw_document_fields(localized.bytes,localized.size,locale_field,&p)||p.preferred_matches>1)out[0]=0;
}
int bw_source_param_metadata(const void *bytes,size_t size,char title[10],char *display,size_t capacity){
    int result=parse(bytes,size,title,display,capacity,NULL);
    if(result&&display&&capacity&&capacity<=4096)memset(display,0,capacity);
    if(!result&&display&&!*display)localized_title(bytes,size,display,capacity);
    return result;
}
int bw_source_param_identity(const void *bytes,size_t size,char title[10],char content[128]){
    if(!content)return -1;int result=parse(bytes,size,title,NULL,0,content);if(result){if(title)memset(title,0,10);memset(content,0,128);}return result;
}
static int document_start(struct json *p,const void *bytes,size_t size){
    if(!bytes||!size||size>BW_NATIVE_DOCUMENT_LIMIT||memchr(bytes,0,size))return -1;
    memset(p,0,sizeof *p);p->data=bytes;p->size=size;p->document=1;space(p);return 0;
}
int bw_document_fields(const void *bytes,size_t size,bw_document_field_fn visit,void *opaque){
    struct json p;if(!visit||document_start(&p,bytes,size))return -1;p.visit=visit;p.opaque=opaque;
    if(object(&p,0))return -1;space(&p);return p.at==p.size?0:-1;
}
int bw_document_array(const void *bytes,size_t size,bw_document_item_fn visit,void *opaque){
    struct json p;if(!visit||document_start(&p,bytes,size)||p.at==p.size||p.data[p.at++]!='[')return -1;
    space(&p);if(p.at<p.size&&p.data[p.at]==']'){p.at++;space(&p);return p.at==p.size?0:-1;}
    for(;;){size_t start=p.at;if(value(&p,1)||visit(opaque,p.data+start,p.at-start))return -1;
        space(&p);if(p.at==p.size)return -1;unsigned c=p.data[p.at++];if(c==']')break;if(c!=',')return -1;space(&p);}
    space(&p);return p.at==p.size?0:-1;
}
int bw_document_string(const void *bytes,size_t size,char *out,size_t capacity){
    struct json p;size_t n;if(!out||!capacity||document_start(&p,bytes,size))return -1;out[0]=0;
    if(string(&p,out,capacity,&n)||n>=capacity||memchr(out,0,n)){out[0]=0;return -1;}
    space(&p);if(p.at!=p.size){out[0]=0;return -1;}return 0;
}

struct registration_document {char title[10],content[128];size_t drm_at,drm_size;};
static int registration_document(const void *bytes,size_t size,struct registration_document *out){
    memset(out,0,sizeof *out);
    if(!bytes||!size||size>BW_SOURCE_PARAM_LIMIT||memchr(bytes,0,size))return -1;
    struct registration_keys *keys=calloc(1,sizeof *keys);if(!keys)return -1;
    struct json p={0};p.data=bytes;p.size=size;p.content=out->content;p.keys=keys;
    if(size>=3&&!memcmp(bytes,"\xef\xbb\xbf",3))p.at=3;
    space(&p);int result=object(&p,0);space(&p);
    if(result||p.at!=size||!p.found||p.content_found!=1)result=-1;
    if(!result){memcpy(out->title,p.title,10);out->drm_at=p.drm_at;out->drm_size=p.drm_size;}
    free(keys);return result;
}
enum bw_registration_param_match bw_registration_param_pair(const void *source,size_t source_size,const void *staged,size_t staged_size){
    struct registration_document a,b;
    if(registration_document(source,source_size,&a)||registration_document(staged,staged_size,&b)||
       strcmp(a.title,b.title)||strcmp(a.content,b.content))return BW_REGISTRATION_PARAM_REFUSE;
    if(source_size==staged_size&&!memcmp(source,staged,source_size))return BW_REGISTRATION_PARAM_EXACT;
    if(source_size<2||staged_size!=source_size-2||a.drm_size!=12||b.drm_size!=10||a.drm_at!=b.drm_at||
       memcmp((const uint8_t *)source+a.drm_at,"\"upgradable\"",12)||memcmp((const uint8_t *)staged+b.drm_at,"\"standard\"",10))return BW_REGISTRATION_PARAM_REFUSE;
    size_t first=source_size;
    for(size_t i=0;i+10<=source_size;i++)if(!memcmp((const uint8_t *)source+i,"upgradable",10)){first=i;break;}
    if(first!=a.drm_at+1)return BW_REGISTRATION_PARAM_REFUSE;
    size_t tail=source_size-first-10;
    if(memcmp(source,staged,first)||memcmp((const uint8_t *)staged+first,"standard",8)||
       memcmp((const uint8_t *)source+first+10,(const uint8_t *)staged+first+8,tail))return BW_REGISTRATION_PARAM_REFUSE;
    return BW_REGISTRATION_PARAM_SHADOWMOUNT_DRM;
}
