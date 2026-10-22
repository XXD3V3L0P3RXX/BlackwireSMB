#include "native_request.h"
#include "native_json.h"
struct parser {const unsigned char *bytes;size_t size,at;};
void bw_native_sensitive_clear(void *memory,size_t size){volatile unsigned char *p=memory;while(size--)*p++=0;}
static void space(struct parser *p){while(p->at<p->size&&(p->bytes[p->at]==' '||p->bytes[p->at]=='\t'||p->bytes[p->at]=='\r'||p->bytes[p->at]=='\n'))p->at++;}
static int hex(struct parser *p,uint32_t *out){
    if(p->size-p->at<4)return -1;uint32_t value=0;
    for(unsigned i=0;i<4;i++){unsigned c=p->bytes[p->at++],n;if(c>='0'&&c<='9')n=c-'0';else if(c>='a'&&c<='f')n=c-'a'+10;else if(c>='A'&&c<='F')n=c-'A'+10;else return -1;value=(value<<4)|n;}
    *out=value;return 0;
}
static int add(char *out,size_t capacity,size_t *used,uint32_t c){
    unsigned char bytes[4];size_t size;
    if(c<0x80){bytes[0]=(unsigned char)c;size=1;}
    else if(c<0x800){bytes[0]=(unsigned char)(0xc0|(c>>6));bytes[1]=(unsigned char)(0x80|(c&63));size=2;}
    else if(c<0x10000){bytes[0]=(unsigned char)(0xe0|(c>>12));bytes[1]=(unsigned char)(0x80|((c>>6)&63));bytes[2]=(unsigned char)(0x80|(c&63));size=3;}
    else{bytes[0]=(unsigned char)(0xf0|(c>>18));bytes[1]=(unsigned char)(0x80|((c>>12)&63));bytes[2]=(unsigned char)(0x80|((c>>6)&63));bytes[3]=(unsigned char)(0x80|(c&63));size=4;}
    if(c<32||c==127||size>=capacity-*used)return -1;memcpy(out+*used,bytes,size);*used+=size;return 0;
}
static int string(struct parser *p,char *out,size_t capacity){
    if(p->at>=p->size||p->bytes[p->at++]!='"')return -1;size_t used=0;
    while(p->at<p->size){
        unsigned char c=p->bytes[p->at++];if(c=='"'){out[used]=0;return bw_json_utf8(out)?0:-1;}
        if(c=='\\'){
            if(p->at>=p->size)return -1;c=p->bytes[p->at++];
            if(c=='u'){
                uint32_t value;if(hex(p,&value))return -1;
                if(value>=0xd800&&value<=0xdbff){
                    if(p->size-p->at<6||p->bytes[p->at]!='\\'||p->bytes[p->at+1]!='u')return -1;
                    p->at+=2;uint32_t low;if(hex(p,&low)||low<0xdc00||low>0xdfff)return -1;value=0x10000+((value-0xd800)<<10)+(low-0xdc00);
                }else if(value>=0xdc00&&value<=0xdfff)return -1;
                if(add(out,capacity,&used,value))return -1;continue;
            }
            if(c!='"'&&c!='\\'&&c!='/')return -1;
        }
        if(c<32||c==127||used+1>=capacity)return -1;out[used++]=(char)c;
    }return -1;
}
static int number(struct parser *p,uint32_t *out){
    if(p->at>=p->size||p->bytes[p->at]<'0'||p->bytes[p->at]>'9')return -1;
    uint32_t value=0;size_t start=p->at;
    while(p->at<p->size&&p->bytes[p->at]>='0'&&p->bytes[p->at]<='9'){
        unsigned c=p->bytes[p->at++]-'0';if(value>(UINT32_MAX-c)/10)return -1;value=value*10+c;
    }
    if(p->at-start>1&&p->bytes[start]=='0')return -1;*out=value;return 0;
}
int bw_native_setup_request_parse(const void *bytes,size_t size,struct bw_native_setup_request *out){
    if(!out)return -1;memset(out,0,sizeof *out);
    if(!bytes||!size||size>4096||memchr(bytes,0,size))return -1;
    struct parser p={bytes,size,0};unsigned seen=0;space(&p);if(p.at==size||p.bytes[p.at++]!='{')goto bad;
    space(&p);if(p.at<size&&p.bytes[p.at]=='}')goto bad;
    for(;;){
        char key[32];if(string(&p,key,sizeof key))goto bad;space(&p);if(p.at==size||p.bytes[p.at++]!=':')goto bad;space(&p);
        unsigned bit=0;char *target=NULL;size_t capacity=0;
        if(!strcmp(key,"server")){bit=1;target=out->values.server;capacity=sizeof out->values.server;}
        else if(!strcmp(key,"share")){bit=2;target=out->values.share;capacity=sizeof out->values.share;}
        else if(!strcmp(key,"share2")){bit=64;target=out->values.share2;capacity=sizeof out->values.share2;out->share2_present=1;}
        else if(!strcmp(key,"smbUser")){bit=4;target=out->values.user;capacity=sizeof out->values.user;}
        else if(!strcmp(key,"smbPassword")){bit=8;target=out->values.password;capacity=sizeof out->values.password;}
        else if(!strcmp(key,"revision"))bit=16;else if(!strcmp(key,"keepCredentials"))bit=32;else goto bad;
        if(seen&bit)goto bad;seen|=bit;
        if(target){if(string(&p,target,capacity))goto bad;}
        else if(bit==16){if(number(&p,&out->revision))goto bad;}
        else if(p.size-p.at>=4&&!memcmp(p.bytes+p.at,"true",4)){out->keep_credentials=1;p.at+=4;}
        else if(p.size-p.at>=5&&!memcmp(p.bytes+p.at,"false",5)){out->keep_credentials=0;p.at+=5;}
        else goto bad;
        space(&p);if(p.at==size)goto bad;unsigned c=p.bytes[p.at++];if(c=='}')break;if(c!=',')goto bad;space(&p);
    }
    space(&p);
    if(p.at!=size||(seen&51)!=51||!out->revision||!*out->values.server||!*out->values.share||strpbrk(out->values.server,"/\\")||strpbrk(out->values.share,"/\\:")||strpbrk(out->values.share2,"/\\:")||
        (out->keep_credentials?(seen&12)!=0:(seen&12)!=12))goto bad;
    return 0;
bad:
    bw_native_sensitive_clear(out,sizeof *out);return -1;
}
int bw_native_send_request_parse(const void *bytes,size_t size,struct bw_native_send_request *out){
    if(!out)return -1;memset(out,0,sizeof *out);if(!bytes||!size||size>4096||memchr(bytes,0,size))return -1;
    struct parser p={bytes,size,0};unsigned seen=0;space(&p);if(p.at==size||p.bytes[p.at++]!='{')goto bad;
    for(;;){
        space(&p);char key[32];if(string(&p,key,sizeof key))goto bad;space(&p);if(p.at==size||p.bytes[p.at++]!=':')goto bad;space(&p);unsigned bit=0;
        if(!strcmp(key,"name"))bit=1;else if(!strcmp(key,"libraryGeneration"))bit=2;else if(!strcmp(key,"settingsRevision"))bit=4;else if(!strcmp(key,"sourceBinding"))bit=8;else goto bad;
        if(seen&bit)goto bad;seen|=bit;
        if(bit==1){if(string(&p,out->source,sizeof out->source))goto bad;}
        else if(bit==8){
            if(string(&p,out->source_binding,sizeof out->source_binding)||strlen(out->source_binding)!=74||memcmp(out->source_binding,"PPSA",4)||out->source_binding[9]!='_')goto bad;
            for(unsigned i=4;i<9;i++)if(out->source_binding[i]<'0'||out->source_binding[i]>'9')goto bad;
            if(strspn(out->source_binding+10,"0123456789abcdef")!=64)goto bad;
        }
        else if(bit==4){if(number(&p,&out->settings_revision))goto bad;}
        else{
            if(p.at==size||p.bytes[p.at]<'0'||p.bytes[p.at]>'9')goto bad;size_t start=p.at;uint64_t value=0;
            while(p.at<size&&p.bytes[p.at]>='0'&&p.bytes[p.at]<='9'){unsigned c=p.bytes[p.at++]-'0';if(value>(UINT64_C(9007199254740991)-c)/10)goto bad;value=value*10+c;}
            if(p.at-start>1&&p.bytes[start]=='0')goto bad;out->library_generation=value;
        }
        space(&p);if(p.at==size)goto bad;unsigned c=p.bytes[p.at++];if(c=='}')break;if(c!=',')goto bad;
    }
    space(&p);if(p.at!=size||(seen&7)!=7||!out->library_generation||!out->settings_revision||bw_manifest_validate_path(out->source,strlen(out->source)))goto bad;
    return 0;
bad:
    bw_native_sensitive_clear(out,sizeof *out);return -1;
}
