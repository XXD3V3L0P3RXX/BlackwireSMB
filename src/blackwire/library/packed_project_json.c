#include "packed_project_json.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>
enum jmode {J_ANY,J_ROOT,J_MODULES,J_MODULE,J_NAME,J_VERSION};
struct json {const uint8_t *p;size_t n,at;unsigned nodes;char (*modules)[65];unsigned count;uint8_t *keys;size_t key_at;int (*current)(void *);void *opaque;};
static int jguard(struct json *j){return j->current&&j->current(j->opaque)?-1:0;}
static void ws(struct json *j){while(j->at<j->n&&(j->p[j->at]==' '||j->p[j->at]=='\r'||j->p[j->at]=='\n'||j->p[j->at]=='\t'))j->at++;}
static int hex4(struct json *j,uint32_t *out){
    if(j->n-j->at<4)return -1;uint32_t v=0;
    for(unsigned i=0;i<4;i++){uint8_t c=j->p[j->at++];unsigned d;if(c>='0'&&c<='9')d=(unsigned)(c-'0');else if(c>='a'&&c<='f')d=(unsigned)(c-'a')+10;else if(c>='A'&&c<='F')d=(unsigned)(c-'A')+10;else return -1;v=v*16+d;}*out=v;return 0;
}
static size_t encode(uint32_t cp,uint8_t out[4]){
    if(cp<128){out[0]=(uint8_t)cp;return 1;}if(cp<2048){out[0]=(uint8_t)(192|(cp>>6));out[1]=(uint8_t)(128|(cp&63));return 2;}
    if(cp<65536){out[0]=(uint8_t)(224|(cp>>12));out[1]=(uint8_t)(128|((cp>>6)&63));out[2]=(uint8_t)(128|(cp&63));return 3;}
    out[0]=(uint8_t)(240|(cp>>18));out[1]=(uint8_t)(128|((cp>>12)&63));out[2]=(uint8_t)(128|((cp>>6)&63));out[3]=(uint8_t)(128|(cp&63));return 4;
}
/* Escaped lone surrogates are retained in an internal comparison encoding,
 * matching Python JSON's permitted unrelated strings; never emitted to UI. */
static int string(struct json *j,uint8_t *out,size_t cap,size_t *bytes,unsigned *characters){
    if(j->at==j->n||j->p[j->at++]!='"')return -1;size_t used=0;unsigned count=0;
    while(j->at<j->n){
        uint32_t cp=j->p[j->at++];if(cp=='"'){*bytes=used;*characters=count;return 0;}if(cp<32)return -1;
        if(cp=='\\'){
            if(j->at==j->n)return -1;uint8_t c=j->p[j->at++];
            if(c=='"'||c=='\\'||c=='/')cp=c;else if(c=='b')cp=8;else if(c=='f')cp=12;else if(c=='n')cp=10;else if(c=='r')cp=13;else if(c=='t')cp=9;
            else if(c=='u'){
                if(hex4(j,&cp))return -1;
                if(cp>=0xd800&&cp<=0xdbff&&j->n-j->at>=6&&j->p[j->at]=='\\'&&j->p[j->at+1]=='u'){
                    size_t before=j->at;j->at+=2;uint32_t low;if(hex4(j,&low))return -1;
                    if(low>=0xdc00&&low<=0xdfff)cp=0x10000+((cp-0xd800)<<10)+(low-0xdc00);else j->at=before;
                }
            }else return -1;
        }else if(cp>=128){
            unsigned more;uint32_t min;
            if(cp>=0xc2&&cp<=0xdf){more=1;min=128;cp&=31;}else if(cp>=0xe0&&cp<=0xef){more=2;min=2048;cp&=15;}else if(cp>=0xf0&&cp<=0xf4){more=3;min=65536;cp&=7;}else return -1;
            if(j->n-j->at<more)return -1;for(unsigned i=0;i<more;i++){uint8_t b=j->p[j->at++];if((b&192)!=128)return -1;cp=(cp<<6)|(b&63);}
            if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return -1;
        }
        uint8_t encoded[4];size_t z=encode(cp,encoded);if(z>cap-used)return -1;if(out)memcpy(out+used,encoded,z);used+=z;count++;
        if((count&1023u)==0&&jguard(j))return -1;
    }return -1;
}
static int value(struct json *,unsigned,enum jmode);
static int keyeq(const uint8_t *key,size_t n,const char *literal){return n==strlen(literal)&&!memcmp(key,literal,n);}
static int object(struct json *j,unsigned depth,enum jmode mode){
    size_t offsets[64],sizes[64],saved=j->key_at;unsigned keys=0,version=0,modules=0,name=0;j->at++;ws(j);
    if(j->at<j->n&&j->p[j->at]=='}'){j->at++;goto done;}
    for(;;){
        if(keys==64||j->key_at>j->n)return -1;size_t size=0;unsigned chars=0;
        if(string(j,j->keys+j->key_at,j->n-j->key_at,&size,&chars)||chars>128)return -1;
        for(unsigned k=0;k<keys;k++)if(size==sizes[k]&&!memcmp(j->keys+offsets[k],j->keys+j->key_at,size))return -1;
        size_t offset=j->key_at;offsets[keys]=offset;sizes[keys]=size;keys++;j->key_at+=size;
        ws(j);if(j->at==j->n||j->p[j->at++]!=':')return -1;ws(j);enum jmode child=J_ANY;
        if(mode==J_ROOT&&keyeq(j->keys+offset,size,"FileVersion")){child=J_VERSION;version=1;}
        if(mode==J_ROOT&&keyeq(j->keys+offset,size,"Modules")){child=J_MODULES;modules=1;}
        if(mode==J_MODULE&&keyeq(j->keys+offset,size,"Name")){child=J_NAME;name=1;}
        if(value(j,depth+1,child))return -1;ws(j);if(j->at==j->n)return -1;uint8_t next=j->p[j->at++];if(next=='}')break;if(next!=',')return -1;ws(j);
    }
done:
    j->key_at=saved;
    if(mode==J_ROOT&&(!version||!modules))return -1;if(mode==J_MODULE&&!name)return -1;return 0;
}
static int array(struct json *j,unsigned depth,enum jmode mode){
    unsigned count=0;j->at++;ws(j);if(j->at<j->n&&j->p[j->at]==']'){j->at++;return 0;}
    for(;;){if(mode==J_MODULES&&count>=32)return -1;if(value(j,depth+1,mode==J_MODULES?J_MODULE:J_ANY))return -1;count++;
        ws(j);if(j->at==j->n)return -1;uint8_t c=j->p[j->at++];if(c==']')return 0;if(c!=',')return -1;ws(j);}
}
static int number(struct json *j,enum jmode mode){
    size_t start=j->at;int negative=0,floating=0;if(j->p[j->at]=='-'){negative=1;j->at++;}size_t digits=j->at;
    if(j->at==j->n)return -1;if(j->p[j->at]=='0')j->at++;else {if(j->p[j->at]<'1'||j->p[j->at]>'9')return -1;while(j->at<j->n&&j->p[j->at]>='0'&&j->p[j->at]<='9')j->at++;}
    size_t integer_digits=j->at-digits;
    if(j->at<j->n&&j->p[j->at]=='.'){floating=1;j->at++;size_t at=j->at;while(j->at<j->n&&j->p[j->at]>='0'&&j->p[j->at]<='9')j->at++;if(at==j->at)return -1;}
    if(j->at<j->n&&(j->p[j->at]=='e'||j->p[j->at]=='E')){floating=1;j->at++;if(j->at<j->n&&(j->p[j->at]=='+'||j->p[j->at]=='-'))j->at++;size_t at=j->at;while(j->at<j->n&&j->p[j->at]>='0'&&j->p[j->at]<='9')j->at++;if(at==j->at)return -1;}
    if(mode==J_VERSION)return j->at-start==1&&j->p[start]=='3'?0:-1;
    if(!floating){const char *limit=negative?"9223372036854775808":"18446744073709551615";size_t z=strlen(limit);if(integer_digits>20||integer_digits>z||(integer_digits==z&&memcmp(j->p+digits,limit,z)>0))return -1;}
    else {size_t n=j->at-start;char *copy=malloc(n+1);if(!copy)return -1;memcpy(copy,j->p+start,n);copy[n]=0;char *end=NULL;double d=strtod(copy,&end);int bad=end!=copy+n||!(d>=-DBL_MAX&&d<=DBL_MAX);free(copy);if(bad)return -1;}
    return 0;
}
static int value(struct json *j,unsigned depth,enum jmode mode){
    if(depth>16||++j->nodes>4096||jguard(j))return -1;ws(j);if(j->at==j->n)return -1;uint8_t c=j->p[j->at];
    if(mode==J_ROOT||mode==J_MODULE){if(c!='{')return -1;return object(j,depth,mode);}
    if(mode==J_MODULES){if(c!='[')return -1;return array(j,depth,mode);}
    if(mode==J_NAME){if(c!='"'||j->count>=32)return -1;size_t n=0;unsigned chars=0;uint8_t output[64];if(string(j,output,sizeof output,&n,&chars)||!n||n>64)return -1;
        for(size_t i=0;i<n;i++)if(!((output[i]>='A'&&output[i]<='Z')||(output[i]>='a'&&output[i]<='z')||(output[i]>='0'&&output[i]<='9')||output[i]=='_'))return -1;
        memcpy(j->modules[j->count],output,n);j->modules[j->count][n]=0;j->count++;return 0;}
    if(mode==J_VERSION)return c=='-'||(c>='0'&&c<='9')?number(j,mode):-1;
    if(c=='{')return object(j,depth,J_ANY);if(c=='[')return array(j,depth,J_ANY);if(c=='"'){size_t n;unsigned chars;return string(j,NULL,j->n,&n,&chars);}
    if(c=='-'||(c>='0'&&c<='9'))return number(j,J_ANY);
    const char *literal=c=='t'?"true":c=='f'?"false":c=='n'?"null":NULL;if(!literal)return -1;size_t n=strlen(literal);if(n>j->n-j->at||memcmp(j->p+j->at,literal,n))return -1;j->at+=n;return 0;
}
int bw_pp_json(const void *raw,size_t size,char modules[32][65],unsigned *count,int (*current)(void *),void *opaque){
    if(count)*count=0;if(modules)memset(modules,0,32*65);if(!raw||!size||size>65536||!modules||!count||!current)return -1;
    struct json j={.p=raw,.n=size,.modules=modules,.current=current,.opaque=opaque};j.keys=malloc(size+1);if(!j.keys)return -1;
    if(size>=3&&j.p[0]==0xef&&j.p[1]==0xbb&&j.p[2]==0xbf)j.at=3;
    int bad=value(&j,0,J_ROOT);ws(&j);if(j.at!=size||jguard(&j))bad=-1;free(j.keys);
    if(bad){memset(modules,0,32*65);return -1;}*count=j.count;return 0;
}
