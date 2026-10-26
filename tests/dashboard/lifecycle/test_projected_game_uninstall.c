/* Actual offline projection inventory/deletion helpers with explicit synthetic
 * parent receipts. This does NOT claim the normal Send/unregister API ran. */
#define _DEFAULT_SOURCE 1
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
static int fail_close,failed_close;static unsigned close_calls;
static int projected_close(int fd){int inject=0;char file[64],name[2048];snprintf(file,sizeof file,"/proc/self/fd/%d",fd);
    ssize_t n=readlink(file,name,sizeof name-1);if(n>=0){name[n]=0;inject=fail_close&&!failed_close&&strstr(name,"/source.original.files")!=NULL;}
    close_calls++;int result=close(fd);if(inject){failed_close=1;errno=EIO;return -1;}return result;}
#define close projected_close
#include "native_game_uninstall.c"
#undef close
#include "source_plan.h"
static unsigned checks,cases,guard_calls;static int canceled;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"projected uninstall case%u line%d: %s\n",cases,__LINE__,#x);exit(2);}}while(0)
static int current(void *v){C(v);guard_calls++;return canceled?-1:0;}
static void path(char out[2048],const char *base,const char *leaf){int n=snprintf(out,2048,"%s/%s",base,leaf);C(n>0&&n<2048);}
static void write_file(const char *name,const void *body,size_t size,mode_t mode){int fd=open(name,O_CREAT|O_EXCL|O_WRONLY,mode);C(fd>=0&&!fchmod(fd,mode));size_t at=0;
    while(at<size){ssize_t n=write(fd,(const unsigned char *)body+at,size-at);C(n>0);at+=(size_t)n;}C(!close(fd));}
static void mkdir_path(const char *name){char copy[2048];C(strlen(name)<sizeof copy);strcpy(copy,name);for(char *at=copy+1;*at;at++)if(*at=='/'){*at=0;C(!mkdir(copy,0755)||errno==EEXIST);*at='/';}C(!mkdir(copy,0755)||errno==EEXIST);}
static void write_relative(const char *base,const char *leaf,const void *body,size_t size,mode_t mode){char name[2048];path(name,base,leaf);char *slash=strrchr(name,'/');C(slash);*slash=0;mkdir_path(name);*slash='/';write_file(name,body,size,mode);}
static void mutate(const char *name){int fd=open(name,O_RDWR);unsigned char byte;C(fd>=0&&pread(fd,&byte,1,0)==1);byte^=1;C(pwrite(fd,&byte,1,0)==1&&!close(fd));}
struct fixture {struct gu_plan p;struct bw_game_uninstall_request request;struct bw_game_uninstall_result result;
    struct bw_source_map original;unsigned char *digests;size_t digest_size;struct bw_companion_projection *projection;
    char base[2048],source[2048],root[2048],state[2048];unsigned char body[512],generation[32];};
static void setup(struct fixture *f,const char *base,unsigned count,unsigned space){
    memset(f,0,sizeof *f);gu_init(&f->p);f->p.r=&f->request;f->p.out=&f->result;C(strlen(base)<sizeof f->base);strcpy(f->base,base);
    f->request.identity.kind=BW_PROVIDER_FOLDER;f->request.identity.policy=BW_PROVIDER_POLICY_V1;struct bw_source_binding *b=&f->request.identity.binding;
    strcpy(b->title,"PPSA99999");strcpy(b->source,"Fixture");strcpy(b->game,"PPSA99999_");memset(b->sha256,0x31,32);for(unsigned i=0;i<32;i++){b->game[10+2*i]='3';b->game[11+2*i]='1';}b->game[74]=0;
    strcpy(f->request.operation,"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb");strcpy(f->request.confirmed_title,b->title);strcpy(f->request.prepared.game,b->game);
    strcpy(f->request.prepared.run,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");strcpy(f->request.prepared.prepared_component,b->game);f->request.prepared.state=BW_PREPARED_VALID;f->request.prepared.prepared_policy=3;
    f->request.ops.current=current;f->request.ops.opaque=f;memset(f->body,0x47,sizeof f->body);memset(f->generation,0x3a,sizeof f->generation);
    path(f->source,base,"source");mkdir_path(f->source);char data[2048],games[2048],scope[2048];path(data,base,"data");mkdir_path(data);path(games,data,"games");mkdir_path(games);path(scope,data,"native-prepared");mkdir_path(scope);
    path(f->root,games,b->game);mkdir_path(f->root);path(f->state,scope,b->game);mkdir_path(f->state);
    struct gu_plan *p=&f->p;p->data=gu_open_root(p,data,p->anchor,p->ids);p->games=gu_open_root(p,games,p->anchor+1,p->ids+1);p->scope=gu_open_root(p,scope,p->anchor+2,p->ids+2);
    p->root=gu_open_root(p,f->root,p->anchor+4,p->ids+4);p->state=gu_open_root(p,f->state,p->anchor+5,p->ids+5);C(p->data>=0&&p->games>=0&&p->scope>=0&&p->root>=0&&p->state>=0);
    struct bw_source_plan plan;C(!bw_source_plan_init_policy(&plan,"fixture",BW_SOURCE_PLAN_COMPANIONS_V1));
    C(!bw_source_plan_add(&plan,"eboot.bin",sizeof f->body,17));write_relative(f->source,"eboot.bin",f->body,sizeof f->body,0755);
    C(!bw_source_plan_add(&plan,"assets/stream.pak",1024*1024,17));
    for(unsigned i=0;i<count;i++){char name[128];snprintf(name,sizeof name,"%s/%s%04u.%s",space==1?"fakelib":"fakelib2",i?"foreign":"README",i,i?"sprx":"txt");C(!bw_source_plan_add(&plan,name,sizeof f->body,17));write_relative(f->source,name,f->body,sizeof f->body,(mode_t)bw_startup_mode(name));}
    char ampr[80];snprintf(ampr,sizeof ampr,"%s/libSceAmpr.sprx",space==1?"fakelib":"fakelib2");C(!bw_source_plan_add(&plan,ampr,sizeof f->body,17));write_relative(f->source,ampr,f->body,sizeof f->body,0755);
    C(!bw_source_plan_finish(&plan,&f->original));bw_source_plan_destroy(&plan);struct bw_manifest original;C(!bw_manifest_open(&original,f->original.bytes,f->original.size));
    f->digest_size=56u+32u*original.count;f->digests=calloc(f->digest_size,1);C(f->digests);memcpy(f->digests,"BWDIG001",8);gu_put32(f->digests+8,1);gu_put32(f->digests+12,original.count);gu_put64(f->digests+16,original.checksum);cp_sha_bytes(f->original.bytes,f->original.size,f->digests+24);
    for(uint32_t i=0;i<original.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&original,i,&row));if(row.flags==BW_MANIFEST_LOCAL)cp_sha_bytes(f->body,sizeof f->body,f->digests+56u+32u*i);}
    struct bw_companion_binding binding={.provider=&f->request.identity,.prepare_run=f->request.prepared.run,.source_generation_sha256=f->generation,
        .roots={(uint64_t)p->ids[4].st_dev,(uint64_t)p->ids[4].st_ino,(uint64_t)p->ids[5].st_dev,(uint64_t)p->ids[5].st_ino}};
    struct bw_companion_source_tables source={f->original.bytes,f->original.size,f->digests,f->digest_size};C(!bw_companion_projection_create(&f->projection,&binding,&source,space,NULL));struct bw_companion_tables t;C(!bw_companion_projection_tables(f->projection,&t));
    p->map=malloc(t.map_size);p->digests=malloc(t.digest_size);C(p->map&&p->digests);memcpy(p->map,t.map,t.map_size);memcpy(p->digests,t.digests,t.digest_size);p->map_size=t.map_size;p->digest_size=t.digest_size;C(!bw_manifest_open(&p->manifest,p->map,p->map_size));
    for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&p->manifest,i,&row));if(row.flags==BW_MANIFEST_LOCAL)write_relative(f->root,row.path,!strcmp(row.path,BW_COMPANION_RECEIPT_PATH)?t.receipt:f->body,(size_t)row.size,(mode_t)bw_startup_mode(row.path));
        else{char full[2048];path(full,f->root,row.path);char *slash=strrchr(full,'/');*slash=0;mkdir_path(full);*slash='/';int fd=open(full,O_CREAT|O_EXCL|O_WRONLY,0644);C(fd>=0&&!ftruncate(fd,(off_t)row.size)&&!close(fd));}}
    write_relative(f->state,BW_COMPANION_ORIGINAL_MAP_NAME,f->original.bytes,f->original.size,0600);write_relative(f->state,BW_COMPANION_ORIGINAL_DIGEST_NAME,f->digests,f->digest_size,0600);
    write_relative(f->root,"blackwire.files",p->map,p->map_size,0644);write_relative(f->root,"blackwire.generation.digests",p->digests,p->digest_size,0644);
    unsigned char *r=p->receipt;memcpy(r,"BWNAT001",8);gu_put32(r+8,1);gu_put32(r+12,384);memcpy(r+16,f->request.prepared.run,32);memcpy(r+48,b->sha256,32);memcpy(r+80,f->generation,32);
    cp_sha_bytes(p->map,p->map_size,r+112);cp_sha_bytes(p->digests,p->digest_size,r+144);gu_put32(r+268,p->manifest.count);gu_put64(r+280,p->map_size);gu_put64(r+288,p->digest_size);
    gu_put64(r+304,p->ids[4].st_dev);gu_put64(r+312,p->ids[4].st_ino);gu_put64(r+320,p->ids[5].st_dev);gu_put64(r+328,p->ids[5].st_ino);cp_sha_bytes(r,352,r+352);cp_sha_bytes(r,384,f->request.prepared.record_sha256);
    cp_sha_bytes(t.receipt,t.receipt_size,f->request.prepared.provenance_sha256);f->request.prepared.provenance_size=t.receipt_size;
    write_relative(f->state,"prepared.bin",r,384,0600);p->rows=calloc(p->manifest.count,sizeof *p->rows);C(p->rows);for(uint32_t i=0;i<p->manifest.count;i++){struct bw_manifest_record row;C(!bw_manifest_get(&p->manifest,i,&row));p->rows[i].path=row.path;p->rows[i].id=i;}qsort(p->rows,p->manifest.count,sizeof *p->rows,gu_row_compare);
    C(!gu_identity(&f->request)&&!gu_receipt(p,r,384,1));
}
static void end(struct fixture *f){fail_close=0;C(!gu_close(&f->p));bw_companion_projection_clear(&f->projection);bw_source_map_destroy(&f->original);free(f->digests);}
static int inventory(struct fixture *f){return gu_walk(&f->p,f->p.root,"",0,0)||gu_walk(&f->p,f->p.state,"",1,0)?-1:0;}
static void reader(struct fixture *f){static const char bytes[]="qualified selected reader";write_relative(f->root,"fakelib2/libSceAmpr.sprx",bytes,sizeof bytes,0755);f->request.reader_size=sizeof bytes;cp_sha_bytes(bytes,sizeof bytes,f->request.reader_sha256);}
int main(int argc,char **argv){C(argc==2);const char *modes[]={"many-f2","many-f1","reader-absent","missing-map","missing-dig","map-mode","map-body","dig-body","receipt-body","component","provenance","extra-legacy","extra-metadata","transient-projection","reader-body","reader-mode","local-body","local-mode","map-linked","map-hardlinked","late-map-replaced","late-map-body","close-unknown","cancel"};
    for(unsigned mode=0;mode<sizeof modes/sizeof modes[0];mode++){cases++;char base[2048],name[2048],other[2048];path(base,argv[1],modes[mode]);mkdir_path(base);struct fixture f;setup(&f,base,140,mode==1?1:2);C(f.original.size>4096&&f.digest_size>4096);if(mode!=2)reader(&f);
        path(name,f.state,BW_COMPANION_ORIGINAL_MAP_NAME);
        if(mode==3)C(!unlink(name));if(mode==4){path(name,f.state,BW_COMPANION_ORIGINAL_DIGEST_NAME);C(!unlink(name));}
        if(mode==5)C(!chmod(name,0644));if(mode==6)mutate(name);if(mode==7){path(name,f.state,BW_COMPANION_ORIGINAL_DIGEST_NAME);mutate(name);}
        if(mode==8){path(name,f.root,BW_COMPANION_RECEIPT_PATH);mutate(name);}if(mode==9)f.request.prepared.prepared_component[0]='Q';if(mode==10)f.request.prepared.provenance_sha256[0]^=1;
        if(mode==11)write_relative(f.root,"fakelib2/libSceAppContent.sprx",f.body,sizeof f.body,0755);
        if(mode==12)write_relative(f.root,"fakelib2/unowned.txt",f.body,sizeof f.body,0644);
        if(mode==13)write_relative(f.root,"fakelib2/blackwire-reader.projection",f.body,sizeof f.body,0600);
        if(mode==14||mode==15){path(name,f.root,"fakelib2/libSceAmpr.sprx");if(mode==14)mutate(name);else C(!chmod(name,0644));}
        if(mode==16||mode==17){path(name,f.root,"fakelib2/foreign0001.sprx");if(mode==16)mutate(name);else C(!chmod(name,0644));}
        if(mode==18||mode==19){path(other,f.base,"original-map");C(!rename(name,other));C(!(mode==18?symlink(other,name):link(other,name)));}
        C(!fstat(f.p.root,f.p.ids+4)&&!fstat(f.p.state,f.p.ids+5));
        fail_close=mode==22;failed_close=0;canceled=mode==23;
        int qualified=gu_identity(&f.request);if(!qualified)qualified=gu_projection(&f.p);if(mode==20||mode==21){C(!qualified);path(name,f.state,BW_COMPANION_ORIGINAL_MAP_NAME);if(mode==20){path(other,f.base,"replaced-map");C(!rename(name,other));write_file(name,f.original.bytes,f.original.size,0600);}else mutate(name);}
        int checked=qualified?qualified:inventory(&f);canceled=0;
        if(mode<3){C(!checked&&f.p.projection);gu_inventory(&f.p);C(!gu_revalidate(&f.p));f.result.files_total=f.p.files_total;C(!gu_delete_nodes(&f.p,0)&&!gu_delete_nodes(&f.p,1));struct stat st;C(lstat(f.root,&st)<0&&errno==ENOENT&&lstat(f.state,&st)<0&&errno==ENOENT);C(f.result.files_removed==f.p.files_total&&f.p.files_total>140);}
        else C(checked<0&&!f.result.mutation_started&&!f.result.files_removed);
        if(mode==22)C(failed_close&&f.p.release_unknown);end(&f);
    }
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualInventoryAndDeleteHelpers\":true,\"explicitSyntheticParentReceipts\":true,\"actualSendOrUnregisterProved\":false,\"offlineOnly\":true,\"guardCalls\":%u,\"closeCalls\":%u,\"consoleActions\":0}\n",cases,checks,guard_calls,close_calls);return 0;
}
