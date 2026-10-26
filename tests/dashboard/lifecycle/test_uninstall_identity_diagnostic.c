/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual local identity predicates and bounded refusal logging. No console. */
#include "native_game_uninstall.c"

static unsigned checks,cases,serial;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"check failed line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static int authority;
static int current(void *unused){(void)unused;return authority;}
static struct bw_game_uninstall_request request;
static char game[2048],record[2048];
static void begin(struct gu_plan *p,const char *base){
    char root[2048];int n=snprintf(root,sizeof root,"%s/case-%u",base,++serial);CHECK(n>0&&(size_t)n<sizeof root);CHECK(!mkdir(root,0700));
    n=snprintf(game,sizeof game,"%s/game",root);CHECK(n>0&&(size_t)n<sizeof game);CHECK(!mkdir(game,0700));
    n=snprintf(record,sizeof record,"%s/record",root);CHECK(n>0&&(size_t)n<sizeof record);CHECK(!mkdir(record,0700));
    memset(p,0,sizeof *p);gu_init(p);memset(&request,0,sizeof request);strcpy(request.operation,"11111111111111111111111111111111");authority=0;request.ops.current=current;p->r=&request;
    p->root=gu_open_root(game,p->anchor+4,p->ids+4);p->state=gu_open_root(record,p->anchor+5,p->ids+5);CHECK(p->root>=0&&p->state>=0);
    CHECK(!unlink(BW_SEND_LOG_PATH)||errno==ENOENT);cases++;
}
static void end(struct gu_plan *p){CHECK(!close(p->root));CHECK(!close(p->state));}
static void refresh(struct gu_plan *p){CHECK(!fstat(p->root,p->ids+4));CHECK(!fstat(p->state,p->ids+5));}
static struct gu_node file_node(struct gu_plan *p,const char *path,unsigned state){
    int root=state?p->state:p->root;int fd=openat(root,path,O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);CHECK(fd>=0);CHECK(write(fd,"fixture",7)==7);CHECK(!close(fd));
    struct gu_node node={.path=(char *)path,.state=state};CHECK(!fstatat(root,path,&node.st,AT_SYMLINK_NOFOLLOW));refresh(p);return node;
}
static void change(struct stat *st,unsigned field){
    switch(field){case 0:st->st_dev++;break;case 1:st->st_ino++;break;case 2:st->st_mode^=0001;break;case 3:st->st_uid++;break;case 4:st->st_gid++;break;case 5:st->st_nlink++;break;
    case 6:st->st_size++;break;case 7:st->st_blocks++;break;case 8:st->st_mtim.tv_sec++;break;case 9:st->st_mtim.tv_nsec++;break;case 10:st->st_ctim.tv_sec++;break;case 11:st->st_ctim.tv_nsec++;break;default:CHECK(0);}
}
static void log_is(const char *check,unsigned area,unsigned compared,unsigned changed,int error,const char *node){
    char body[4096],needle[512];int fd=open(BW_SEND_LOG_PATH,O_RDONLY|O_NOFOLLOW);CHECK(fd>=0);ssize_t n=read(fd,body,sizeof body-1);CHECK(n>0&&(size_t)n<sizeof body-1);body[n]=0;CHECK(!close(fd));
    CHECK(!strncmp(body,"sendlog.1 ",10));CHECK(strstr(body,"event=uninstall_identity ")!=NULL);
    CHECK(strstr(body,"operation=11111111111111111111111111111111 currentCall=")!=NULL);
    snprintf(needle,sizeof needle,"check=%s area=%u node=\"%s\"",check,area,node);CHECK(strstr(body,needle)!=NULL);
    snprintf(needle,sizeof needle,"errno=%d compared=%u changed=%u stat=%u",error,compared,changed,compared!=0);CHECK(strstr(body,needle)!=NULL);
    CHECK(strchr(body,'\n')==body+n-1);CHECK(strstr(body,"expected=[")!=NULL&&strstr(body,"observed=[")!=NULL);
}
int main(int argc,char **argv){
    CHECK(argc==2);struct gu_plan p;int parent;char leaf[256];
    /* Every compared field is diagnosed, and no predicate has been relaxed. */
    for(unsigned i=0;i<12;i++){
        begin(&p,argv[1]);struct gu_node node=file_node(&p,"ordinary.bin",0);change(&node.st,i);parent=-1;
        CHECK(gu_named(&p,&node,1,&parent,leaf)<0);CHECK(parent==-1);log_is("node_named_changed",4,4095,1u<<i,0,"ordinary.bin");end(&p);
    }
    for(unsigned i=0;i<12;i++){
        begin(&p,argv[1]);change(p.ids+4,i);CHECK(gu_revalidate(&p)<0);
        log_is(i<5?"anchor_held_changed":"root_changed",4,i<5?31:4095,1u<<i,0,"");end(&p);
    }
    /* A cache-equivalent node comparison still ignores size/timestamps only
     * when the unchanged caller explicitly requested node-only identity. */
    begin(&p,argv[1]);struct gu_node node=file_node(&p,"ordinary.bin",0);change(&node.st,6);parent=-1;
    CHECK(!gu_named(&p,&node,0,&parent,leaf));CHECK(parent>=0);CHECK(!close(parent));CHECK(!p.identity_diagnostic);end(&p);
    begin(&p,argv[1]);node=file_node(&p,"ordinary.bin",1);change(&node.st,7);parent=-1;
    CHECK(gu_named(&p,&node,1,&parent,leaf)<0);log_is("node_named_changed",5,4095,128,0,"ordinary.bin");end(&p);
    begin(&p,argv[1]);authority=-1;CHECK(gu_revalidate(&p)<0);CHECK(p.current_calls==1);log_is("authority_current",0,0,0,0,"");end(&p);
    begin(&p,argv[1]);request.ops.current=NULL;CHECK(gu_revalidate(&p)<0);log_is("authority_current",0,0,0,0,"");end(&p);
    begin(&p,argv[1]);node=file_node(&p,"missing.bin",0);CHECK(!unlinkat(p.root,"missing.bin",0));refresh(&p);parent=-1;
    CHECK(gu_named(&p,&node,1,&parent,leaf)<0);log_is("node_named_stat",4,0,0,ENOENT,"missing.bin");end(&p);
    begin(&p,argv[1]);node=(struct gu_node){.path="absent/file.bin"};parent=-1;
    CHECK(gu_named(&p,&node,1,&parent,leaf)<0);log_is("node_parent_open",4,0,0,ENOENT,"absent/file.bin");end(&p);
    begin(&p,argv[1]);CHECK(!mkdirat(p.root,"nested",0700));node=file_node(&p,"nested/file.bin",0);parent=-1;
    CHECK(gu_named(&p,&node,1,&parent,leaf)<0);log_is("node_parent_inventory",4,0,0,0,"nested/file.bin");end(&p);
    begin(&p,argv[1]);CHECK(!mkdirat(p.root,"nested",0700));node=file_node(&p,"nested/file.bin",0);struct gu_node directory={.path="nested"};CHECK(!fstatat(p.root,"nested",&directory.st,AT_SYMLINK_NOFOLLOW));change(&directory.st,1);p.nodes=&directory;p.count=1;parent=-1;
    CHECK(gu_named(&p,&node,1,&parent,leaf)<0);log_is("node_parent_changed",4,31,2,0,"nested/file.bin");p.nodes=NULL;p.count=0;end(&p);
    begin(&p,argv[1]);char moved[2100];snprintf(moved,sizeof moved,"%s-moved",game);CHECK(!rename(game,moved));CHECK(gu_revalidate(&p)<0);log_is("anchor_named_current",4,0,0,0,"");end(&p);
    /* Diagnostic output is one copied record, escapes hostile names, preserves
     * errno, and never changes the guarded prepared tree. */
    begin(&p,argv[1]);struct stat before,after;CHECK(!fstat(p.root,&before));errno=E2BIG;
    gu_identity_diag(&p,"escaped",4,"a\n\"\\ b",EIO,0,NULL,NULL);CHECK(errno==E2BIG);
    gu_identity_diag(&p,"suppressed",5,"second",0,0,NULL,NULL);log_is("escaped",4,0,0,EIO,"a\\x0a\\x22\\x5c\\x20b");
    CHECK(!fstat(p.root,&after));CHECK(gu_same(&before,&after));end(&p);
    begin(&p,argv[1]);char long_name[1024];memset(long_name,'\n',sizeof long_name-1);long_name[sizeof long_name-1]=0;
    gu_identity_diag(&p,"bounded",4,long_name,0,0,NULL,NULL);int fd=open(BW_SEND_LOG_PATH,O_RDONLY);CHECK(fd>=0);char line[2048];ssize_t size=read(fd,line,sizeof line-1);CHECK(size>0&&size<1200);line[size]=0;CHECK(strstr(line,"truncated=1")!=NULL);CHECK(!close(fd));end(&p);
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualIdentityChecks\":true,\"diagnosticsOnly\":true,\"consoleActions\":0}\n",cases,checks);return 0;
}
