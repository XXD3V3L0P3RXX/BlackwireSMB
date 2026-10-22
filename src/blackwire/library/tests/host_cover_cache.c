/* Exercise the maintained cache, response and state code. Source/settings and
 * action callbacks are explicit fixtures; no SMB or console is contacted. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#include "../native_backend.c"
#include <assert.h>

static unsigned checks,cases,settings_calls,action_calls;
#define CHECK(value) do {checks++;assert(value);} while(0)
static const unsigned char art_a[]={137,80,78,71,13,10,26,10,'A'};
static const unsigned char art_b[]={137,80,78,71,13,10,26,10,'B'};
static const unsigned char art_c[]={137,80,78,71,13,10,26,10,'C'};

struct fixture {
    struct bw_native_backend backend;
    struct bw_catalog_item items[8];
    struct bw_native_cover covers[8];
};
static int settings(void *opaque,struct bw_share_config *config,struct bw_setup_generation *generation){
    struct fixture *f=opaque;memset(config,0,sizeof *config);
    strcpy(config->server,"192.0.2.10");strcpy(config->share,"OwnedBackups");
    *generation=f->backend.settings_generation;settings_calls++;return BW_SETUP_VALID;
}
static int fixture_prepare(void *opaque,struct bw_share_config *config,const struct bw_native_prepare_selection *selection,
                   int (*current)(void *),void *guard,bw_source_observer_fn observe,void *observer,struct bw_native_prepared *out){
    (void)opaque;(void)config;(void)selection;(void)current;(void)guard;(void)observe;(void)observer;(void)out;
    action_calls++;return BW_SEND_FAILED;
}
static int fixture_play(void *opaque,const struct bw_native_play_selection *selection,int (*current)(void *),void *guard,
                void (*observe)(void *,const struct bw_reader_event *),void *observer){
    (void)opaque;(void)selection;(void)current;(void)guard;(void)observe;(void)observer;action_calls++;return -1;
}
static int close_game(void *opaque,const char *run){(void)opaque;(void)run;action_calls++;return -1;}
static void metadata(struct bw_catalog_item *item,const char *source,const char *title,enum bw_catalog_state kind){
    memset(item,0,sizeof *item);strcpy(item->source,source);strcpy(item->name,"Preserved source");strcpy(item->title,"Preserved title");
    strcpy(item->title_id,title);item->state=kind;
    if(kind==BW_CATALOG_FOLDER){item->boot.size=100;item->param.size=110;}
    if(kind==BW_CATALOG_CONTAINER_READY){
        item->container_kind=BW_CONTAINER_PFSC_PFS;item->container_bytes=4096;item->logical_boot_size=100;
        item->image.smb2_type=SMB2_TYPE_FILE;item->image.smb2_size=4096;item->image.smb2_ino=42;
        bw_source_catalog_image_hash(&item->image,item->image_sha256);
    }
}
static void init(struct fixture *f){
    memset(f,0,sizeof *f);struct bw_native_backend *p=&f->backend;
    CHECK(!pthread_mutex_init(&p->lock,NULL));p->phase=BACKEND_READY;p->generation=7;
    p->settings_enabled=p->settings_known=1;p->settings_status=BW_SETUP_VALID;p->settings_revision=1;
    p->settings_generation.root_device=1;p->settings_generation.root_inode=2;
    strcpy(p->server,"192.0.2.10");strcpy(p->share,"OwnedBackups");p->opaque=f;
    p->ops.read_settings=settings;p->ops.prepare=fixture_prepare;p->ops.play=fixture_play;p->ops.close=close_game;
    p->jobs=bw_native_jobs_create();CHECK(p->jobs!=NULL);
    p->library.catalog.items=f->items;p->library.catalog.count=p->library.catalog.capacity=8;
    p->library.covers=f->covers;p->library.cover_count=8;
    p->library.requests=31;p->library.entries=9;p->library.content_bytes=711;
    metadata(&f->items[0],"A/First folder","PPSA99991",BW_CATALOG_FOLDER);
    metadata(&f->items[1],"B/Own folder","PPSA99991",BW_CATALOG_FOLDER);
    metadata(&f->items[2],"C/No icon folder","PPSA99991",BW_CATALOG_FOLDER);
    metadata(&f->items[3],"D/Other title","PPSA99992",BW_CATALOG_FOLDER);
    metadata(&f->items[4],"E/Preserved.pfsc","PPSA99991",BW_CATALOG_CONTAINER_READY);
    metadata(&f->items[5],"F/Unverified.pfsc","",BW_CATALOG_CONTAINER);
    metadata(&f->items[6],"G/Missing donor.pfsc","PPSA99993",BW_CATALOG_CONTAINER_READY);
    metadata(&f->items[7],"H/Invalid metadata","",BW_CATALOG_BAD_PARAM);
    const unsigned char *art[]={art_a,art_b,art_c};const unsigned indices[]={0,1,3};
    for(unsigned i=0;i<3;i++){
        unsigned index=indices[i];f->covers[index]=(struct bw_native_cover){(uint8_t *)art[i],sizeof art_a};
        f->items[index].cover=1;f->items[index].icon.size=sizeof art_a;p->library.cover_bytes+=sizeof art_a;
    }
    CHECK(valid_library(&p->library));
}
static void request(struct fixture *f,const char *query,int expected,const unsigned char *art){
    struct bw_native_response response={0};unsigned before_settings=settings_calls,before_actions=action_calls;
    struct bw_native_library before=f->backend.library;struct bw_catalog_item items[8];struct bw_native_cover covers[8];
    memcpy(items,f->items,sizeof items);memcpy(covers,f->covers,sizeof covers);
    CHECK(!cover(&f->backend,query,&response));CHECK(response.status==expected);
    if(expected==200){CHECK(!strcmp(response.type,"image/png"));CHECK(response.size==sizeof art_a);CHECK(!memcmp(response.bytes,art,sizeof art_a));}
    CHECK(!memcmp(&before,&f->backend.library,sizeof before));CHECK(!memcmp(items,f->items,sizeof items));CHECK(!memcmp(covers,f->covers,sizeof covers));
    CHECK(settings_calls==before_settings);CHECK(action_calls==before_actions);bw_native_response_clear(&response);cases++;
}
int main(void){
    struct fixture f;init(&f);
    request(&f,"name=A%2FFirst%20folder&v=7",200,art_a);
    request(&f,"name=B%2FOwn%20folder&v=7",200,art_b); /* Own bytes beat the earlier matching donor. */
    request(&f,"name=C%2FNo%20icon%20folder&v=7",200,art_a);
    request(&f,"name=E%2FPreserved.pfsc&v=7",200,art_a);
    request(&f,"name=E%2FPreserved.pfsc",200,art_a); /* Existing unversioned route remains supported. */
    request(&f,"name=D%2FOther%20title&v=7",200,art_c);
    request(&f,"name=G%2FMissing%20donor.pfsc&v=7",404,NULL);
    request(&f,"name=F%2FUnverified.pfsc&v=7",404,NULL);
    request(&f,"name=H%2FInvalid%20metadata&v=7",404,NULL);
    request(&f,"name=Unknown&v=7",404,NULL);
    request(&f,"name=E%2FPreserved.pfsc&v=6",409,NULL);
    request(&f,"name=E%2FPreserved.pfsc&v=8",409,NULL);
    request(&f,"name=E%2FPreserved.pfsc&v=bad",400,NULL);
    request(&f,"name=E%2FPreserved.pfsc&v=18446744073709551616",400,NULL);
    strcpy(f.items[4].title_id,"PPSA99994");CHECK(valid_library(&f.backend.library));
    request(&f,"name=E%2FPreserved.pfsc&v=7",404,NULL); /* Similar title/source text is not a match. */
    strcpy(f.items[4].title_id,"PPSA99991");
    struct bw_native_cover *saved=f.backend.library.covers;
    f.backend.library.covers=NULL;f.backend.library.cover_count=0;f.backend.library.cover_bytes=0;CHECK(valid_library(&f.backend.library));
    request(&f,"name=E%2FPreserved.pfsc&v=7",404,NULL);
    f.backend.library.covers=saved;f.backend.library.cover_count=8;f.backend.library.cover_bytes=3*sizeof art_a;
    /* A container is never admitted as a cache owner/donor by the unchanged
     * library validator, even if it has the same title and supplied bytes. */
    f.covers[4]=(struct bw_native_cover){(uint8_t *)art_a,sizeof art_a};
    f.items[4].cover=1;f.items[4].icon.size=sizeof art_a;f.backend.library.cover_bytes+=sizeof art_a;
    CHECK(!valid_library(&f.backend.library));cases++;
    f.covers[4]=(struct bw_native_cover){0};f.items[4].cover=0;memset(&f.items[4].icon,0,sizeof f.items[4].icon);
    f.backend.library.cover_bytes-=sizeof art_a;CHECK(valid_library(&f.backend.library));
    struct bw_native_response response={0};CHECK(!state(&f.backend,&response));CHECK(response.status==200);
    CHECK(!action_calls);CHECK(f.backend.library.requests==31&&f.backend.library.content_bytes==711&&f.backend.library.cover_bytes==27);
    printf("{\"cases\":%u,\"checks\":%u,\"actionCalls\":%u,\"state\":",cases,checks,action_calls);
    CHECK(fwrite(response.bytes,1,response.size,stdout)==response.size);puts("}");bw_native_response_clear(&response);
    CHECK(!bw_native_jobs_destroy(f.backend.jobs));CHECK(!pthread_mutex_destroy(&f.backend.lock));return 0;
}
