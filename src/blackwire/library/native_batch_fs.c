static int bs_same(const struct stat *a,const struct stat *b){return cp_measure_same(a,b)&&S_ISREG(b->st_mode)&&b->st_nlink==1;}
static int bs_nonce(const char *value){
    if(!value||strnlen(value,33)!=32)return 0;
    unsigned nonzero=0;for(unsigned i=0;i<32;i++){if(!strchr("0123456789abcdef",value[i]))return 0;nonzero|=value[i]!='0';}return nonzero!=0;
}
static int bs_paths(const char *base,const struct bw_source_binding *binding,char state[2048]){
    if(!base||!binding||strnlen(binding->game,sizeof binding->game)!=74||strnlen(binding->source,sizeof binding->source)>BW_MANIFEST_MAX_PATH||
        bw_manifest_validate_path(binding->source,strlen(binding->source))||strnlen(binding->title,sizeof binding->title)!=9||memcmp(binding->title,"PPSA",4)||
        memcmp(binding->game,binding->title,9)||binding->game[9]!='_'||!v2_game_component(binding->game))return -1;
    for(unsigned i=4;i<9;i++)if(binding->title[i]<'0'||binding->title[i]>'9')return -1;
    char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);if(strcmp(binding->game+10,hash))return -1;
    int a=snprintf(bs_games_path,sizeof bs_games_path,"%s/games",base),b=snprintf(state,2048,"%s/native-batch-prepared/%s",base,binding->game);
    return a<0||(size_t)a>=sizeof bs_games_path||b<0||b>=2048?-1:0;
}
static int bs_state_leaf(char leaf[140],const struct bw_source_binding *original,const struct bw_derived_prepared_identity *prepared){
    if(!original||!prepared||strnlen(original->game,sizeof original->game)!=74||strnlen(prepared->component,sizeof prepared->component)!=74||
       memcmp(original->game,prepared->component,9)||prepared->component[9]!='_')return -1;
    char hex[65];for(unsigned i=0;i<32;i++)snprintf(hex+2*i,3,"%02x",prepared->namespace_sha256[i]);
    if(strcmp(prepared->component+10,hex))return -1;
    int n=snprintf(leaf,140,"%s.%s",original->game,hex);return n!=139?-1:0;
}
static int bs_root(const char *path,struct prepare_v2 *anchor){
    memset(anchor,0,sizeof *anchor);if(v2_copy(anchor->root,sizeof anchor->root,path))return -1;
    int fd=open(path,O_RDONLY|O_DIRECTORY|O_NOFOLLOW);if(fd>=0&&!v2_root_current(anchor,fd)){close(fd);fd=-1;}return fd;
}
static int bs_directory(const char *path,int parent,const char *leaf,int create,int *created){
    struct prepare_v2 anchor={0};*created=0;if(v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,parent)||!v2_game_component(leaf))return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);
    if(fd<0&&errno==ENOENT&&create){if(v2_mkdirat(parent,leaf,0755))return -1;*created=1;fd=v2_openat(parent,leaf,O_RDONLY|O_DIRECTORY|O_NOFOLLOW,0);if(fd>=0&&fchmod(fd,0755)){close(fd);return -1;}}
    char child[2048];int n=snprintf(child,sizeof child,"%s/%s",path,leaf);
    if(fd<0||n<0||(size_t)n>=sizeof child){if(fd>=0)close(fd);return -1;}
    if(!v2_root_current(&anchor,parent)||v2_copy(anchor.root,sizeof anchor.root,child)||!v2_root_current(&anchor,fd)||(create&&fsync(parent))){close(fd);return -1;}return fd;
}
static int bs_read(int parent,const char *leaf,size_t maximum,void **bytes,size_t *size,struct stat *identity){
    *bytes=NULL;*size=0;int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return errno==ENOENT?1:-1;
    struct stat before,after;int result=-1;void *body=NULL;
    if(fstat(fd,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_uid!=geteuid()||before.st_size<1||(uint64_t)before.st_size>maximum||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,NULL))goto done;
    body=malloc((size_t)before.st_size);if(!body||v2_read_all(fd,body,(size_t)before.st_size)||cp_regular(parent,leaf,fd,(uint64_t)before.st_size,&after)||!bs_same(&before,&after))goto done;
    if(identity)*identity=after;*bytes=body;*size=(size_t)before.st_size;body=NULL;result=0;
done:
    free(body);if(close(fd))result=-1;return result;
}
static int bs_exact(int parent,const char *leaf,const void *expected,size_t size){
    void *bytes=NULL;size_t got=0;int result=bs_read(parent,leaf,size,&bytes,&got,NULL);int bad=result||got!=size||(got==size&&memcmp(bytes,expected,size));free(bytes);return bad?-1:0;
}
static int bs_binding_json(const struct bw_source_binding *binding,struct bw_json *json){
    bw_json_text(json,"{\"version\":1,\"source\":");bw_json_string(json,binding->source);bw_json_text(json,",\"game\":");bw_json_string(json,binding->game);
    bw_json_text(json,",\"titleId\":");bw_json_string(json,binding->title);char hash[65];for(unsigned i=0;i<32;i++)snprintf(hash+2*i,3,"%02x",binding->sha256[i]);
    bw_json_text(json,",\"bindingSha256\":");bw_json_string(json,hash);bw_json_text(json,"}\n");return json->bad?-1:0;
}
static int bs_provider_json(const struct bw_provider_binding *identity,struct bw_json *json){
    if(!identity||identity->policy!=BW_PROVIDER_POLICY_V1||
       (identity->kind!=BW_PROVIDER_FOLDER&&identity->kind!=BW_PROVIDER_CONTAINER))return -1;
    if(bs_binding_json(&identity->binding,json))return -1;
    if(identity->kind==BW_PROVIDER_CONTAINER){
        if(json->used<2)return -1;json->used-=2;
        bw_json_text(json,",\"sourceKind\":\"container\",\"sourcePolicy\":\"unsigned-contiguous-d32-pfs-v1\"}\n");
    }
    return json->bad?-1:0;
}
static int bs_blob(const char *path,int directory,const char *leaf,const void *bytes,size_t size,const char *run,double deadline){
    struct prepare_v2 anchor={0};double at=cp_now();
    if(!bytes||!size||!bs_nonce(run)||at<0||at>=deadline||cp_io_current()||v2_copy(anchor.root,sizeof anchor.root,path)||!v2_root_current(&anchor,directory))return -1;
    /* These metadata names belong to a new private stage. Never adopt a name,
     * replace it, or remove an uncertain partial write on this path. */
    int fd=v2_openat(directory,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0644);if(fd<0)return -1;
    int bad=1;struct stat before,after;uint8_t wanted[32],got[32];cp_sha_bytes(bytes,size,wanted);
    if(fchmod(fd,0644)||cp_regular(directory,leaf,fd,0,&before)||before.st_uid!=geteuid()||cp_write_all(fd,bytes,size)||fsync(fd)||
       cp_regular(directory,leaf,fd,size,&after)||!v2_identity(&before,&after)||after.st_uid!=geteuid()||(after.st_mode&07777)!=0644||
       cp_file_hash(fd,size,got)||memcmp(got,wanted,32)||!v2_root_current(&anchor,directory)||cp_io_current()||fsync(directory))goto done;
    at=cp_now();if(at<0||at>=deadline)goto done;bad=0;
done:if(close(fd))bad=1;return bad?-1:0;
}
static int bs_existing(const struct cp_plan *plan,int root,int parent,const char *relative,const char *leaf,uint64_t size,const uint8_t hash[32]){
    if(!cp_alive(plan)||cp_io_current())return -1;
    int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0)return -1;
    struct stat before,after;uint8_t actual[32];
    char full[BW_MANIFEST_MAX_PATH+1];int n=snprintf(full,sizeof full,"%s%s%s",relative,*relative?"/":"",leaf);
    mode_t mode=n>=0&&(size_t)n<sizeof full?(mode_t)bw_startup_mode(full):0;
    int bad=n<0||(size_t)n>=sizeof full||!v2_directory_current(&plan->cfg,root,parent,relative)||cp_regular(parent,leaf,fd,size,&before)||
        before.st_uid!=geteuid()||(before.st_mode&07777)!=mode||cp_file_hash(fd,size,actual)||memcmp(hash,actual,32)||
        cp_regular(parent,leaf,fd,size,&after)||!bs_same(&before,&after)||after.st_uid!=geteuid()||!v2_directory_current(&plan->cfg,root,parent,relative)||
        !cp_alive(plan)||cp_io_current();
    if(close(fd))bad=1;return bad?-1:0;
}
static int bs_lock(int directory){
    static const char marker[]="Blackwire native send lock v1\n";const char *leaf=".blackwire-send.lock";
    int fd=v2_openat(directory,leaf,O_CREAT|O_EXCL|O_NOFOLLOW|O_RDWR,0600),created=fd>=0;
    if(fd<0&&errno==EEXIST)fd=v2_openat(directory,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);
    if(fd<0)return -1;
    char bytes[sizeof marker];struct stat st;
    if((created&&(fchmod(fd,0600)||cp_write_all(fd,marker,sizeof marker-1)||fsync(fd)||fsync(directory)))||
        cp_regular(directory,leaf,fd,sizeof marker-1,&st)||pread(fd,bytes,sizeof marker-1,0)!=(ssize_t)(sizeof marker-1)||memcmp(bytes,marker,sizeof marker-1)||flock(fd,LOCK_EX|LOCK_NB)){close(fd);return -1;}return fd;
}
static int bs_games_stopped(const char *path){
    if(v2_games_stopped(path))return -1;
    struct {char title[16];int pid,active;} rows[128]={0};unsigned count=0;FILE *file=fopen(path,"r");if(!file)return -1;char line[1024];int bad=0;
    while(fgets(line,sizeof line,file)){
        if(!strchr(line,'\n')){bad=1;break;}
        char *at=NULL;int active=1;
        if((at=strstr(line,"[GAME] started: PPSA")))at+=strlen("[GAME] started: ");
        else if((at=strstr(line,"[GAME] process exited, awaiting same-app handoff: PPSA")))at+=strlen("[GAME] process exited, awaiting same-app handoff: ");
        else if((at=strstr(line,"[GAME] finalizing stopped application: PPSA"))){at+=strlen("[GAME] finalizing stopped application: ");active=0;}else continue;
        char title[16]={0};int pid=0;if(sscanf(at,"%15s pid=%d",title,&pid)!=2||strlen(title)!=9||strncmp(title,"PPSA",4)||pid<=0){bad=1;break;}
        for(unsigned i=4;i<9;i++)if(title[i]<'0'||title[i]>'9')bad=1;
        unsigned i=0;for(;i<count;i++)if(!strcmp(rows[i].title,title))break;
        if(i==count){if(count==128){bad=1;break;}memcpy(rows[i].title,title,sizeof title);count++;}
        if(active){rows[i].pid=pid;rows[i].active=1;}else if(rows[i].pid==pid)rows[i].active=0;
    }
    int read_failed=ferror(file),close_failed=fclose(file);if(read_failed||close_failed)bad=1;for(unsigned i=0;i<count;i++)if(rows[i].active)bad=1;return bad?-1:0;
}
/* The cooperative append-only ShadowMount writer may add ordinary scanner
 * lines while a hidden stage is built. Parse a fresh complete idle log when
 * it grows; an unchanged held/named inode needs only its metadata check.
 * Replacement, shrink, same-size rewrite or change during parsing refuses.
 * This optimization is not an idle authority or a hostile-writer guarantee. */
static int bs_log_current(struct bs_operation *o){
    const char *path=o->request->game_log;
    if(o->log_file<0){o->log_file=open(path,O_RDONLY|O_NOFOLLOW|O_NONBLOCK);if(o->log_file<0)return -1;}
    struct stat before,named,after;
    if(fstat(o->log_file,&before)||lstat(path,&named)||!bs_same(&before,&named)||before.st_uid!=geteuid()||before.st_size<0||(uint64_t)before.st_size>UINT64_C(16777216))return -1;
    if(o->log_checked&&bs_same(&o->log_identity,&before))return 0;
    if(o->log_checked&&(!v2_identity(&o->log_identity,&before)||before.st_size<=o->log_identity.st_size))return -1;
    if(bs_games_stopped(path)||fstat(o->log_file,&after)||lstat(path,&named)||!bs_same(&before,&after)||!bs_same(&after,&named)||after.st_uid!=geteuid())return -1;
    o->log_identity=after;o->log_checked=1;return 0;
}
static int bs_streams_empty(struct cp_plan *plan,int root,int missing_ok){
    for(uint32_t i=0;i<plan->manifest.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&plan->manifest,i,&row))return -1;if(row.flags!=BW_MANIFEST_STREAM)continue;
        char leaf[256],relative[1024];int parent=v2_parent(&plan->cfg,root,row.path,0,leaf,relative);if(parent==-2&&missing_ok)continue;if(parent<0)return -1;
        int fd=v2_openat(parent,leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);if(fd<0){int absent=errno==ENOENT;close(parent);if(absent&&missing_ok)continue;return -1;}
        struct stat st;int bad=cp_regular(parent,leaf,fd,row.size,&st)||st.st_blocks||!v2_directory_current(&plan->cfg,root,parent,relative);
        if(close(fd))bad=1;if(close(parent))bad=1;if(bad)return -1;
    }return 0;
}
/* Mode5 alone permits hash-checked metadata resident in original STREAM
 * placeholders. Other modes retain the exact empty-STREAM function above. */
#define BS_SEED_UNIT 65536u
struct bs_seed_slot {
    int fd,parent;
    char leaf[256],relative[1024];
    struct stat created;
};
struct bs_seed {
    struct bw_batch_metadata_plan plan;
    struct bs_seed_slot slots[BW_PBB_MAX_ARCHIVES];
    uint8_t buffer[65536];
};
_Static_assert(sizeof(struct bs_seed)<=262144,"bounded metadata FS workspace");
static int bs_seed_close(struct bs_seed *s){
    int bad=0;if(!s)return 0;
    for(uint32_t i=0;i<BW_PBB_MAX_ARCHIVES;i++){
        if(s->slots[i].fd>=0){if(close(s->slots[i].fd))bad=1;s->slots[i].fd=-1;}
        if(s->slots[i].parent>=0){if(close(s->slots[i].parent))bad=1;s->slots[i].parent=-1;}
    }return bad?-1:0;
}
static int bs_seed_destroy(struct bs_seed *s){int bad=bs_seed_close(s);free(s);return bad;}
static int bs_seed_open(const struct bw_batch_facts *f,struct bs_seed **out){
    *out=NULL;if(bw_pbb_prefix_mode(f->bootstrap.prefix)!=5)return 0;
    struct bs_seed *s=calloc(1,sizeof *s);if(!s)return -1;
    for(uint32_t i=0;i<BW_PBB_MAX_ARCHIVES;i++)s->slots[i].fd=s->slots[i].parent=-1;
    const char *error=NULL;if(bw_batch_metadata_build(f,BS_SEED_UNIT,&s->plan,&error)){bs_seed_destroy(s);return -1;}
    *out=s;return 0;
}
static int bs_seed_archive(const struct bs_seed *s,const struct bw_manifest_record *row){
    if(!s)return -1;
    for(uint32_t i=0;i<s->plan.archive_count;i++)if(!strcmp(s->plan.archives[i].path,row->path))return (int)i;
    return -1;
}
static int bs_seed_hold(struct bpo_context *c,const struct bw_manifest_record *row,int fd,int parent,const char *leaf,const char *relative,const struct stat *created){
    int ai=bs_seed_archive(c->seed,row);if(ai<0)return 0;
    struct bs_seed_slot *s=&c->seed->slots[(uint32_t)ai];const struct bw_batch_metadata_archive *a=&c->seed->plan.archives[(uint32_t)ai];
    if(s->fd>=0||s->parent>=0||row->size!=a->info.size||row->size>INT64_MAX||strlen(leaf)>=sizeof s->leaf||strlen(relative)>=sizeof s->relative)return -1;
    s->fd=fd;s->parent=parent;strcpy(s->leaf,leaf);strcpy(s->relative,relative);s->created=*created;return 1;
}
static int bs_seed_identity(struct cp_plan *p,int root,struct bs_seed_slot *s,uint64_t size,struct stat *out){
    struct stat st;if(!cp_alive(p)||cp_io_current()||s->fd<0||s->parent<0||
       cp_regular(s->parent,s->leaf,s->fd,size,&st)||st.st_uid!=geteuid()||(st.st_mode&07777)!=0644||
       !v2_identity(&s->created,&st)||!v2_directory_current(&p->cfg,root,s->parent,s->relative))return -1;
    if(out)*out=st;return 0;
}
/* No owner/cache/provider/current/namespace calls while a range consumer is
 * active. The caller has already validated this held exclusive new file. */
struct bs_metadata_writer {
    int fd;uint32_t archive_index;enum bw_pbb_span_kind kind;
    struct bw_pbb_archive archive;
    uint64_t used;
    struct cp_sha sha256;struct pp_sha1 sha1;
};
static int bs_seed_archive_equal(const struct bw_pbb_archive *a,const struct bw_pbb_archive *b){
    if(strcmp(a->path,b->path)||a->info.size!=b->info.size||a->info.inode!=b->info.inode||a->info.mtime_ticks!=b->info.mtime_ticks||
       a->info.ctime_ticks!=b->info.ctime_ticks||a->info.attributes!=b->info.attributes||a->filename_count!=b->filename_count||a->directory_count!=b->directory_count||a->hash_seed!=b->hash_seed)return 0;
    for(unsigned i=0;i<BW_PBB_SPANS;i++){const struct bw_pbb_span *x=&a->spans[i],*y=&b->spans[i];if(x->offset!=y->offset||x->size!=y->size||memcmp(x->sha1,y->sha1,20)||memcmp(x->sha256,y->sha256,32))return 0;}return 1;
}
static int bs_metadata_write(void *raw,uint32_t ai,enum bw_pbb_span_kind kind,const struct bw_pbb_archive *archive,uint64_t offset,const void *bytes,size_t size){
    struct bs_metadata_writer *w=raw;
    if(!bytes||!archive||!size||size>65536||ai!=w->archive_index||kind!=w->kind||!bs_seed_archive_equal(archive,&w->archive))return -1;
    const struct bw_pbb_span *span=&w->archive.spans[(unsigned)kind];
    if(w->used>span->size||size>span->size-w->used||offset!=span->offset+w->used||offset>INT64_MAX||size>(uint64_t)INT64_MAX-offset)return -1;
    size_t at=0;while(at<size){ssize_t n=pwrite(w->fd,(const uint8_t *)bytes+at,size-at,(off_t)(offset+at));if(n<0&&errno==EINTR)continue;if(n<=0||(size_t)n>size-at)return -1;at+=(size_t)n;}
    cp_sha_update(&w->sha256,bytes,size);pp_sha1_update(&w->sha1,bytes,size);w->used+=(uint64_t)size;return 0;
}
static int bs_seed_emit(struct bpo_context *c){
    struct bs_seed *s=c->seed;if(!s)return 0;
    for(uint32_t i=0;i<s->plan.archive_count;i++){
        struct bs_seed_slot *slot=&s->slots[i];const struct bw_pbb_archive *a=&c->facts->bootstrap.archives[i];
        if(bs_seed_identity(c->plan,c->operation->root,slot,a->info.size,NULL))return -1;
        for(unsigned k=0;k<BW_PBB_SPANS;k++){
            const struct bw_pbb_span *span=&a->spans[k];if(!span->size)continue;
            struct bs_metadata_writer w={.fd=slot->fd,.archive_index=i,.kind=(enum bw_pbb_span_kind)k};
            memcpy(&w.archive,a,sizeof w.archive);cp_sha_init(&w.sha256);pp_sha1_init(&w.sha1);
            if(bw_packed_bootstrap_read_metadata(c->operation->request->plan_request->bootstrap,i,(enum bw_pbb_span_kind)k,bs_metadata_write,&w))return -1;
            uint8_t h256[32],h1[20];cp_sha_final(&w.sha256,h256);pp_sha1_final(&w.sha1,h1);
            if(w.used!=span->size||memcmp(h256,span->sha256,32)||memcmp(h1,span->sha1,20)||
               bs_seed_identity(c->plan,c->operation->root,slot,a->info.size,NULL))return -1;
        }
    }return 0;
}
static int bs_seed_span_hash(struct cp_plan *p,int fd,const struct bw_pbb_span *span,uint8_t buffer[65536]){
    if(!span->size||span->offset>INT64_MAX||span->size>(uint64_t)INT64_MAX-span->offset)return -1;
    struct cp_sha h256;struct pp_sha1 h1;cp_sha_init(&h256);pp_sha1_init(&h1);uint64_t used=0;
    while(used<span->size){
        if(!cp_alive(p)||cp_io_current())return -1;size_t n=span->size-used>65536?65536:(size_t)(span->size-used);ssize_t got;
        do{got=pread(fd,buffer,n,(off_t)(span->offset+used));}while(got<0&&errno==EINTR);
        if(got<=0||(size_t)got>n)return -1;cp_sha_update(&h256,buffer,(size_t)got);pp_sha1_update(&h1,buffer,(size_t)got);used+=(uint64_t)got;
    }
    uint8_t actual256[32],actual1[20];cp_sha_final(&h256,actual256);pp_sha1_final(&h1,actual1);
    return memcmp(actual256,span->sha256,32)||memcmp(actual1,span->sha1,20)?-1:0;
}
static int bs_seed_file(struct cp_plan *p,int root,struct bs_seed *s,uint32_t ai,struct bs_seed_slot *slot,uint64_t *allocated){
    const struct bw_batch_metadata_archive *a=&s->plan.archives[ai];struct stat before,after;
    if(bs_seed_identity(p,root,slot,a->info.size,&before)||before.st_blocks<0||(uint64_t)before.st_blocks>UINT64_MAX/512u)return -1;
    uint64_t blocks=(uint64_t)before.st_blocks*512u;if(blocks>a->allocation_bytes)return -1;
    for(uint32_t i=0;i<s->plan.span_count;i++)if(s->plan.spans[i].archive_index==ai&&bs_seed_span_hash(p,slot->fd,&s->plan.spans[i].span,s->buffer))return -1;
    if(bs_seed_identity(p,root,slot,a->info.size,&after)||!bs_same(&before,&after))return -1;*allocated=blocks;return 0;
}
static void bs_seed_account(const struct bs_seed *s,uint64_t allocated,struct bw_native_batch_metadata_accounting *out){
    memset(out,0,sizeof *out);if(s){out->declared_bytes=s->plan.declared_bytes;out->unique_bytes=s->plan.unique_bytes;out->allocation_ceiling_bytes=s->plan.allocation_bytes;out->observed_allocated_bytes=allocated;}
}
static int bs_seed_finish(struct bpo_context *c){
    struct bs_seed *s=c->seed;if(!s)return 0;uint64_t total=0;
    for(uint32_t i=0;i<s->plan.archive_count;i++){
        struct bs_seed_slot *slot=&s->slots[i];uint64_t actual=0;
        if(bs_seed_identity(c->plan,c->operation->root,slot,s->plan.archives[i].info.size,NULL)||fsync(slot->fd)||
           bs_seed_file(c->plan,c->operation->root,s,i,slot,&actual)||actual>s->plan.allocation_bytes-total||fsync(slot->parent))return -1;total+=actual;
    }
    return bs_seed_close(s);
}
static int bs_streams_verify(struct cp_plan *p,int root,int missing_ok,struct bs_seed *s,struct bw_native_batch_metadata_accounting *accounting){
    if(accounting)memset(accounting,0,sizeof *accounting);if(!s)return bs_streams_empty(p,root,missing_ok);
    if(missing_ok)return -1;uint64_t total=0;unsigned seen[BW_PBB_MAX_ARCHIVES]={0};
    for(uint32_t i=0;i<p->manifest.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&p->manifest,i,&row))return -1;if(row.flags!=BW_MANIFEST_STREAM)continue;
        struct bs_seed_slot slot={.fd=-1,.parent=-1};slot.parent=v2_parent(&p->cfg,root,row.path,0,slot.leaf,slot.relative);if(slot.parent<0)return -1;
        slot.fd=v2_openat(slot.parent,slot.leaf,O_RDONLY|O_NOFOLLOW|O_NONBLOCK,0);int bad=slot.fd<0;int ai=bs_seed_archive(s,&row);uint64_t actual=0;
        if(!bad){bad=cp_regular(slot.parent,slot.leaf,slot.fd,row.size,&slot.created)||slot.created.st_uid!=geteuid()||(slot.created.st_mode&07777)!=0644;
            if(!bad&&ai>=0){uint32_t id=(uint32_t)ai;bad=seen[id]++||row.size!=s->plan.archives[id].info.size||bs_seed_file(p,root,s,id,&slot,&actual)||actual>s->plan.allocation_bytes-total;if(!bad)total+=actual;}
            else if(!bad)bad=slot.created.st_blocks||!cp_alive(p)||cp_io_current()||!v2_directory_current(&p->cfg,root,slot.parent,slot.relative);
        }
        if(slot.fd>=0&&close(slot.fd))bad=1;if(close(slot.parent))bad=1;if(bad)return -1;
    }
    for(uint32_t i=0;i<s->plan.archive_count;i++)if(seen[i]!=1)return -1;
    if(accounting)bs_seed_account(s,total,accounting);return 0;
}
static void bs_hex(char text[65],const uint8_t hash[32]){for(unsigned i=0;i<32;i++)snprintf(text+2*i,3,"%02x",hash[i]);}
static int bs_identity_equal(const struct bw_provider_binding *a,const struct bw_provider_binding *b){
 return a&&b&&a->kind==b->kind&&a->policy==b->policy&&!memcmp(a->binding.sha256,b->binding.sha256,32)&&
 !strcmp(a->binding.game,b->binding.game)&&!strcmp(a->binding.source,b->binding.source)&&!strcmp(a->binding.title,b->binding.title);
}
static int bs_source_json(const struct bw_provider_binding *identity,struct bw_json *j){
 if(!identity||identity->kind!=BW_PROVIDER_FOLDER||identity->policy!=BW_PROVIDER_POLICY_V1)return -1;
 if(bs_binding_json(&identity->binding,j)||j->used<2)return -1;j->used-=2;
 bw_json_text(j,",\"preparedPolicy\":\"native-packed-startup-plan-v2\",\"sourceKind\":1,\"sourcePolicy\":1}\n");return j->bad?-1:0;
}
static int bs_owner_json(const struct bw_batch_facts *f,const uint8_t provenance[32],size_t size,struct bw_json *j){
 char hex[65];bw_json_text(j,"{\"version\":2,\"originalGame\":");bw_json_string(j,f->original.binding.game);
 bw_json_text(j,",\"preparedComponent\":");bw_json_string(j,f->prepared.component);
 bw_json_text(j,",\"preparedPolicy\":2,\"commitmentSha256\":");bs_hex(hex,f->prepared.commitment_sha256);bw_json_string(j,hex);
 bw_json_text(j,",\"materialSha256\":");bs_hex(hex,f->material_sha256);bw_json_string(j,hex);
 bw_json_text(j,",\"provenanceSha256\":");bs_hex(hex,provenance);bw_json_string(j,hex);
 bw_json_format(j,",\"provenanceBytes\":%zu}\n",size);return j->bad?-1:0;
}
static int bs_combined(const struct bw_json *source,const struct bw_json *owner,uint8_t bytes[4096],size_t *size){
 static const char domain[]="Blackwire native packed startup source/root identity v2";size_t n=sizeof domain-1;
 if(source->bad||owner->bad||n+12u+source->used+owner->used>4096)return -1;
 bpo_put32(bytes,(uint32_t)n);memcpy(bytes+4,domain,n);n+=4;bpo_put32(bytes+n,(uint32_t)source->used);n+=4;memcpy(bytes+n,source->bytes,source->used);n+=source->used;
 bpo_put32(bytes+n,(uint32_t)owner->used);n+=4;memcpy(bytes+n,owner->bytes,owner->used);n+=owner->used;*size=n;return 0;
}
static int bs_provenance_name(char leaf[96],const struct bw_derived_prepared_identity *p){
 char hash[65];bs_hex(hash,p->namespace_sha256);int n=snprintf(leaf,96,"provenance-%s.bin",hash);return n<0||n>=96?-1:0;
}
static void bs_ref(struct bw_derived_artifact_ref *r,const void *p,size_t n){r->size=n;cp_sha_bytes(p,n,r->sha256);}
