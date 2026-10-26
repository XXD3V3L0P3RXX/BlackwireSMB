/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual production admission callbacks + planner; generated metadata stream.
 * This is NOT a container/parser/SMB fixture: parser ceilings also count dirs. */
#define _DEFAULT_SOURCE 1
#ifdef BW_NAMESPACE_PREPARATION
#include "library/compressed_data.c"
#define LIMIT_ERROR BW_COMPRESSED_DATA_LIMIT
#else
#include "library/source_catalog.c"
#define LIMIT_ERROR BW_PFS_LIMIT
#endif
#include <stdio.h>
static unsigned checks,cases;
#define C(x) do{checks++;if(!(x)){fprintf(stderr,"namespace line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static int canceled;
#ifdef BW_NAMESPACE_PREPARATION
static int guard(void *unused){(void)unused;return canceled;}
struct state {struct bw_source_plan plan;struct bw_compressed_data owner;struct scanning scan;};
static void init(struct state *p){memset(p,0,sizeof *p);canceled=0;C(!bw_source_plan_init(&p->plan,"PPSA09999_fixture"));p->owner.image=calloc(1,sizeof *p->owner.image);C(p->owner.image);p->owner.current=guard;p->scan.plan=&p->plan;p->scan.owner=&p->owner;}
static int invoke(struct state *p,const char *path,unsigned flags){struct bw_pfs_inode n={.id=1,.logical_size=1,.flags=flags};return add(&p->scan,path,&n);}
static uint32_t files(struct state *p){C(p->scan.count==p->plan.count);return p->scan.count;}
static size_t paths(struct state *p){C(p->scan.path_bytes==p->plan.paths_size);return p->scan.path_bytes;}
static int error(struct state *p){return p->scan.error;}
static void seal(struct state *p){struct bw_source_map map={0};C(!bw_source_plan_finish(&p->plan,&map));struct bw_manifest manifest;C(!bw_manifest_open(&manifest,map.bytes,map.size));C(manifest.count==p->scan.count&&manifest.path_bytes==p->scan.path_bytes);bw_source_map_destroy(&map);}
static void clear(struct state *p){bw_source_plan_destroy(&p->plan);free(p->owner.image);}
#else
struct state {struct bw_source_smb source;struct logical_metadata scan;};
static void init(struct state *p){memset(p,0,sizeof *p);canceled=0;struct timespec t;C(!clock_gettime(CLOCK_MONOTONIC,&t));p->source.smb=(struct smb2_context *)p;p->source.deadline=t.tv_sec+t.tv_nsec/1e9+120;p->scan.source=&p->source;}
static int invoke(struct state *p,const char *path,unsigned flags){struct bw_pfs_inode n={.id=1,.logical_size=1,.flags=flags};if(canceled)p->source.deadline=0;return logical(&p->scan,path,&n);}
static uint32_t files(struct state *p){return p->scan.files;}
static size_t paths(struct state *p){return p->scan.path_bytes;}
static int error(struct state *p){return p->scan.error;}
static void seal(struct state *p){(void)p;}
static void clear(struct state *p){(void)p;}
#endif
static void startup(struct state *p){C(!invoke(p,"eboot.bin",0));C(!invoke(p,"sce_sys/param.json",0));}
int main(void){
 struct state state;
 cases++;init(&state);startup(&state);
 for(uint32_t i=2;i<BW_MANIFEST_MAX_FILES;i++){char path[64];snprintf(path,sizeof path,"assets/f%06u.pak",i);C(!invoke(&state,path,0));}
 C(files(&state)==BW_MANIFEST_MAX_FILES);seal(&state);size_t before=paths(&state);
 cases++;C(invoke(&state,"assets/one-extra.pak",0)==-1&&error(&state)==LIMIT_ERROR);C(files(&state)==BW_MANIFEST_MAX_FILES&&paths(&state)==before);clear(&state);

 cases++;init(&state);startup(&state);unsigned index=0;
 while(paths(&state)<BW_MANIFEST_MAX_PATH_BYTES){
  size_t remain=BW_MANIFEST_MAX_PATH_BYTES-paths(&state),bytes=remain<512?remain:512;
  C(bytes>270&&bytes<=512);char path[513];memset(path,'d',255);path[255]='/';
  size_t leaf=bytes-257;char name[32];int n=snprintf(name,sizeof name,"f%06u-",index++);C(n>0&&(size_t)n<leaf);
  memcpy(path+256,name,(size_t)n);memset(path+256+n,'x',leaf-(size_t)n);path[bytes-1]=0;
  C(!invoke(&state,path,0));
 }
 C(paths(&state)==BW_MANIFEST_MAX_PATH_BYTES&&files(&state)<BW_MANIFEST_MAX_FILES);seal(&state);uint32_t count=files(&state);
 cases++;C(invoke(&state,"extra.pak",0)==-1&&error(&state)==LIMIT_ERROR);C(paths(&state)==BW_MANIFEST_MAX_PATH_BYTES&&files(&state)==count);clear(&state);

 cases++;init(&state);C(!invoke(&state,"ignored.bak",1));C(!files(&state)&&!paths(&state)&&!error(&state));clear(&state);
 cases++;init(&state);C(invoke(&state,"../ignored.bak",1)==-1&&error(&state)==BW_PFS_PATH);C(!files(&state)&&!paths(&state));clear(&state);
 cases++;init(&state);C(invoke(&state,"\xffignored.bak",1)==-1&&error(&state)==BW_PFS_PATH);C(!files(&state)&&!paths(&state));clear(&state);
 cases++;init(&state);C(invoke(&state,"sce_module/startup.prx",1)==-1&&error(&state)==BW_PFS_UNSUPPORTED);C(!files(&state)&&!paths(&state));clear(&state);
 cases++;init(&state);canceled=1;C(invoke(&state,"ignored.bak",1)==-1);C(!files(&state)&&!paths(&state));clear(&state);
 printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"maxFiles\":%u,\"maxPathBytes\":%u,\"actualCallbacks\":true,\"generatedNamespaceModelOnly\":true,\"preparation\":%s}\n",cases,checks,BW_MANIFEST_MAX_FILES,BW_MANIFEST_MAX_PATH_BYTES,
#ifdef BW_NAMESPACE_PREPARATION
 "true"
#else
 "false"
#endif
 );
 return 0;
}
