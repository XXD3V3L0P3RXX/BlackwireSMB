#define _DEFAULT_SOURCE 1
#include "source-selection-under-test.h"
#include "native_removal_request.h"
#include <stdio.h>
#include <sys/stat.h>

static unsigned checks,cases;
#define C(value) do {checks++;if(!(value)){fprintf(stderr,"case %u line %u: %s\n",cases,__LINE__,#value);exit(1);}} while(0)
static struct bw_share_config configured(void){
    struct bw_share_config c={0};strcpy(c.server,"192.0.2.10");strcpy(c.share,"CompressedGames");
    strcpy(c.share2,"FolderGames");strcpy(c.user,"project-fixture");strcpy(c.password,"fixture-only");return c;
}
static void cleared(const void *raw,size_t size){const unsigned char *p=raw;for(size_t i=0;i<size;i++)C(!p[i]);}
static struct bw_catalog_item item(const char *path,const char *share){
    struct bw_catalog_item result={.state=BW_CATALOG_FOLDER};strcpy(result.source,path);strcpy(result.source_share,share);
    strcpy(result.name,path);strcpy(result.title,"Fixture game");strcpy(result.title_id,"PPSA99999");return result;
}
static void shares(void){
    struct bw_share_config c=configured(),one={0},copy={0};cases++;
    C(!bw_share_select(&one,&c,NULL));C(!strcmp(one.share,"CompressedGames")&&!*one.share2);C(!strcmp(one.password,c.password));
    C(!bw_share_select(&one,&c,"FolderGames"));C(!strcmp(one.share,"FolderGames")&&!*one.share2);C(!strcmp(one.user,c.user));
    copy=c;C(!bw_share_select(&copy,&copy,copy.share2));C(!strcmp(copy.share,"FolderGames")&&!*copy.share2);
    cases++;C(bw_share_select(&one,&c,"UnconfiguredShare"));cleared(&one,sizeof one);
    cases++;copy=c;strcpy(copy.share2,"compressedgames");C(!bw_native_setup_config_valid(&copy));C(bw_share_select(&one,&copy,NULL));
    cases++;const char *legacy="server=192.0.2.10\nshare=CompressedGames\nuser=project-fixture\npassword=fixture-only\npath=\n";
    C(!bw_share_parse(&copy,legacy,strlen(legacy)));C(!*copy.share2);C(bw_native_setup_config_valid(&copy));
    cases++;const char *two="server=192.0.2.10\nshare=CompressedGames\nshare2=FolderGames\nuser=project-fixture\npassword=fixture-only\npath=\n";
    C(!bw_share_parse(&copy,two,strlen(two)));C(!strcmp(copy.share2,"FolderGames"));C(bw_native_setup_config_valid(&copy));
    const char *bad[]={"share2=FolderGames\nshare2=Other\n","share2=../Folder\n","share2=Bad:Share\n","share2=compressedgames\n"};
    for(size_t i=0;i<sizeof bad/sizeof bad[0];i++){cases++;char text[512];snprintf(text,sizeof text,"%s%s",legacy,bad[i]);C(bw_share_parse(&copy,text,strlen(text)));cleared(&copy,sizeof copy);}
    bw_share_clear(&c);bw_share_clear(&one);bw_share_clear(&copy);
}
static void bindings(void){
    struct bw_share_config c=configured(),first=c,second={0},probe={0};struct bw_provider_binding a,b,observed,legacy;
    struct bw_catalog_item left=item("Collection/Game","CompressedGames"),right=item("Collection/Game","FolderGames");
    cases++;C(!bw_source_catalog_binding(&a,&c,&left));C(!bw_source_catalog_binding(&b,&c,&right));C(strcmp(a.binding.game,b.binding.game));
    memset(first.share2,0,sizeof first.share2);C(!bw_provider_binding_init(&legacy,&first,left.source,left.title_id,BW_PROVIDER_FOLDER));C(!memcmp(&legacy,&a,sizeof a));
    cases++;probe=c;C(!bw_provider_binding_select(&observed,&probe,right.source,right.title_id,BW_PROVIDER_FOLDER,b.binding.game));
    C(!memcmp(&observed,&b,sizeof b));C(!strcmp(probe.share,"FolderGames")&&!*probe.share2);C(!strcmp(probe.password,c.password));
    cases++;probe=c;C(!bw_provider_binding_select(&observed,&probe,left.source,left.title_id,BW_PROVIDER_FOLDER,a.binding.game));C(!strcmp(probe.share,"CompressedGames")&&!*probe.share2);
    cases++;probe=first;C(bw_provider_binding_select(&observed,&probe,right.source,right.title_id,BW_PROVIDER_FOLDER,b.binding.game));cleared(&probe,sizeof probe);cleared(&observed,sizeof observed);
    cases++;probe=c;char invalid[75];strcpy(invalid,b.binding.game);invalid[73]=invalid[73]=='0'?'1':'0';C(bw_provider_binding_select(&observed,&probe,right.source,right.title_id,BW_PROVIDER_FOLDER,invalid));cleared(&probe,sizeof probe);
    cases++;strcpy(right.source_share,"UnconfiguredShare");C(bw_source_catalog_binding(&observed,&c,&right));cleared(&observed,sizeof observed);
    cases++;right=item("Collection/Game","FolderGames");C(!bw_share_select(&second,&c,"FolderGames"));
    struct bw_provider_binding compressed;C(!bw_provider_binding_init(&compressed,&second,right.source,right.title_id,BW_PROVIDER_CONTAINER));
    probe=c;C(!bw_provider_binding_select(&observed,&probe,right.source,right.title_id,BW_PROVIDER_CONTAINER,compressed.binding.game));C(!strcmp(probe.share,"FolderGames"));
    bw_share_clear(&c);bw_share_clear(&first);bw_share_clear(&second);bw_share_clear(&probe);
}
static void selection(void){
    struct bw_share_config c=configured();struct bw_native_backend *p=calloc(1,sizeof *p);C(p);
    memcpy(p->server,c.server,sizeof p->server);memcpy(p->share,c.share,sizeof p->share);memcpy(p->share2,c.share2,sizeof p->share2);
    struct bw_catalog_item rows[]={item("Same/Game","CompressedGames"),item("Same/Game","FolderGames"),item("Unique","FolderGames")};
    p->library.catalog.items=rows;p->library.catalog.count=3;struct bw_native_send_request request={0};strcpy(request.source,"Same/Game");
    cases++;C(catalog_selected(p,&request)==3);
    struct bw_provider_binding exact;C(!bw_source_catalog_binding(&exact,&c,rows+1));strcpy(request.source_binding,exact.binding.game);
    cases++;C(catalog_selected(p,&request)==1);
    cases++;C(!bw_source_catalog_binding(&exact,&c,rows));strcpy(request.source_binding,exact.binding.game);C(catalog_selected(p,&request)==0);
    cases++;strcpy(request.source,"Unique");C(catalog_selected(p,&request)==3);
    cases++;memset(request.source_binding,0,sizeof request.source_binding);C(catalog_selected(p,&request)==2);
    cases++;strcpy(request.source,"Missing");C(catalog_selected(p,&request)==3);free(p);bw_share_clear(&c);
}
static int idle(void *unused){(void)unused;return 0;}
static void settings(const char *path){
    cases++;C(!mkdir(path,0700));struct bw_share_config c=configured(),read={0};struct bw_setup_generation before={0},after={0};struct bw_setup_saved saved={0};
    C(bw_native_setup_read(path,&read,&before)==BW_SETUP_MISSING);
    C(bw_native_setup_store(path,&c,&before,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",idle,NULL,&saved)==BW_SETUP_SAVE_OK);C(saved.published&&saved.verified);
    C(bw_native_setup_read(path,&read,&after)==BW_SETUP_VALID);C(!strcmp(read.share,c.share)&&!strcmp(read.share2,c.share2));C(!strcmp(read.password,c.password));
    cases++;struct bw_setup_saved refused={0};strcpy(c.share2,"OtherFolder");C(bw_native_setup_store(path,&c,&before,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",idle,NULL,&refused)==BW_SETUP_SAVE_STALE);C(!refused.published);
    cases++;before=after;C(bw_native_setup_store(path,&c,&before,"cccccccccccccccccccccccccccccccc",idle,NULL,&saved)==BW_SETUP_SAVE_OK);
    C(bw_native_setup_read(path,&read,&after)==BW_SETUP_VALID);C(!strcmp(read.share2,"OtherFolder"));C(!bw_native_setup_generation_equal(&before,&after));
    cases++;before=after;memset(c.share2,0,sizeof c.share2);C(bw_native_setup_store(path,&c,&before,"dddddddddddddddddddddddddddddddd",idle,NULL,&saved)==BW_SETUP_SAVE_OK);
    C(bw_native_setup_read(path,&read,&after)==BW_SETUP_VALID);C(!*read.share2);bw_share_clear(&c);bw_share_clear(&read);
}
static void requests(void){
    const char *setup="{\"server\":\"192.0.2.10\",\"share\":\"CompressedGames\",\"share2\":\"FolderGames\",\"revision\":1,\"keepCredentials\":true}";
    struct bw_native_setup_request s;cases++;C(!bw_native_setup_request_parse(setup,strlen(setup),&s));C(s.share2_present&&!strcmp(s.values.share2,"FolderGames"));
    cases++;setup="{\"server\":\"192.0.2.10\",\"share\":\"CompressedGames\",\"share2\":\"\",\"revision\":1,\"keepCredentials\":true}";
    C(!bw_native_setup_request_parse(setup,strlen(setup),&s));C(s.share2_present&&!*s.values.share2);
    cases++;setup="{\"server\":\"192.0.2.10\",\"share\":\"CompressedGames\",\"revision\":1,\"keepCredentials\":true}";
    C(!bw_native_setup_request_parse(setup,strlen(setup),&s));C(!s.share2_present);
    struct bw_share_config c=configured();struct bw_catalog_item row=item("Same/Game","FolderGames");struct bw_provider_binding b;C(!bw_source_catalog_binding(&b,&c,&row));
    char body[512];snprintf(body,sizeof body,"{\"name\":\"Same/Game\",\"libraryGeneration\":7,\"settingsRevision\":3,\"sourceBinding\":\"%s\"}",b.binding.game);
    struct bw_native_send_request r;cases++;C(!bw_native_send_request_parse(body,strlen(body),&r));C(!strcmp(r.source_binding,b.binding.game));
    struct bw_native_removal_request removal;cases++;C(!bw_native_removal_request_parse(body,strlen(body),1,&removal));C(!strcmp(removal.selected.source_binding,b.binding.game));
    snprintf(body,sizeof body,"{\"name\":\"Same/Game\",\"libraryGeneration\":7,\"settingsRevision\":3,\"confirmedTitleId\":\"PPSA99999\",\"sourceBinding\":\"%s\"}",b.binding.game);
    cases++;C(!bw_native_removal_request_parse(body,strlen(body),0,&removal));C(!strcmp(removal.selected.source_binding,b.binding.game));
    cases++;const char *bad="{\"name\":\"Same/Game\",\"libraryGeneration\":7,\"settingsRevision\":3,\"sourceBinding\":\"OtherShare\"}";
    C(bw_native_send_request_parse(bad,strlen(bad),&r));C(bw_native_removal_request_parse(bad,strlen(bad),1,&removal));
    bw_share_clear(&c);bw_native_sensitive_clear(&s,sizeof s);
}
static struct bw_native_library part(const char *first,const char *last,const char *share,unsigned char marker){
    struct bw_native_library p={0};p.catalog.count=p.catalog.capacity=p.cover_count=2;p.catalog.items=calloc(2,sizeof *p.catalog.items);p.covers=calloc(2,sizeof *p.covers);C(p.catalog.items&&p.covers);
    p.catalog.items[0]=item(first,share);p.catalog.items[1]=item(last,share);p.requests=10;p.entries=p.catalog.entries=2;p.catalog.collections=1;
    for(unsigned i=0;i<2;i++){p.catalog.items[i].cover=1;p.catalog.items[i].icon.size=1;p.covers[i].bytes=malloc(1);C(p.covers[i].bytes);*p.covers[i].bytes=marker+i;p.covers[i].size=1;}
    p.cover_bytes=2;return p;
}
static void merge(void){
    struct bw_native_library parts[]={part("A","Same/Game","CompressedGames",1),part("B","Same/Game","FolderGames",3)},out={0};
    cases++;C(!library_merge(&out,parts));C(valid_library(&out));C(out.catalog.count==4&&out.cover_bytes==4&&out.requests==20);
    C(!strcmp(out.catalog.items[0].source,"A")&&*out.covers[0].bytes==1);C(!strcmp(out.catalog.items[1].source,"B")&&*out.covers[1].bytes==3);
    C(!strcmp(out.catalog.items[2].source_share,"CompressedGames")&&*out.covers[2].bytes==2);C(!strcmp(out.catalog.items[3].source_share,"FolderGames")&&*out.covers[3].bytes==4);
    bw_native_library_destroy(&out);for(unsigned i=0;i<2;i++)bw_native_library_destroy(parts+i);
    cases++;memset(parts,0,sizeof parts);C(!library_merge(&out,parts));C(valid_library(&out));bw_native_library_destroy(&out);
    cases++;parts[0]=part("A","Z","CompressedGames",1);parts[1]=part("A","Z","CompressedGames",3);C(library_merge(&out,parts));bw_native_library_destroy(&out);for(unsigned i=0;i<2;i++)bw_native_library_destroy(parts+i);
    cases++;parts[0].catalog.count=BW_CATALOG_MAX_ITEMS;parts[1].catalog.count=1;C(library_merge(&out,parts));memset(parts,0,sizeof parts);
    cases++;parts[0].cover_bytes=BW_NATIVE_COVER_LIMIT;parts[1].cover_bytes=1;C(library_merge(&out,parts));memset(parts,0,sizeof parts);
    cases++;parts[0].requests=UINT32_MAX;parts[1].requests=1;C(library_merge(&out,parts));memset(parts,0,sizeof parts);
    cases++;parts[0].catalog.entries=UINT32_MAX;parts[1].catalog.entries=1;C(library_merge(&out,parts));memset(parts,0,sizeof parts);
}
int main(int argc,char **argv){C(argc==2);shares();bindings();selection();settings(argv[1]);requests();merge();printf("{\"cases\":%u,\"checks\":%u,\"sourceIsolation\":true,\"legacyBindingsPreserved\":true}\n",cases,checks);return 0;}
