#include "native_project_recipe.h"
#include "source_profile.h"
#include "../streamreader/copy_sha256.h"
#include <stdlib.h>
#include <string.h>

static int whitespace(unsigned c){return c==' '||c=='\t'||c=='\r'||c=='\n';}
static unsigned lower(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static int equal_fold(const char *a,const char *b){for(;*a&&*b;a++,b++)if(lower((unsigned char)*a)!=lower((unsigned char)*b))return 0;return *a==*b;}
static int prefix_fold(const unsigned char *p,size_t n,const char *text){size_t m=strlen(text);if(n<m)return 0;for(size_t i=0;i<m;i++)if(lower(p[i])!=lower((unsigned char)text[i]))return 0;return 1;}
static int suffix(const char *name,const char *end){size_t n=strlen(name),m=strlen(end);return n>=m&&equal_fold(name+n-m,end);}
static int fail(struct bw_native_project_recipe *p,const char *error){p->error=error;return -1;}

int bw_native_project_command(const void *bytes,size_t size,char request[250],char destination[241]){
    if(request)request[0]=0;if(destination)destination[0]=0;
    if(!bytes||!request||!destination||!size||size>BW_NATIVE_PROJECT_COMMAND_LIMIT)return -1;
    const unsigned char *p=bytes;size_t at=0;unsigned found=0;
    if(size>=3&&p[0]==0xef&&p[1]==0xbb&&p[2]==0xbf)at=3;
    for(size_t i=at;i<size;i++)if(!p[i]||p[i]>=127||(!whitespace(p[i])&&p[i]<32))return -1;
    char result[250]={0};
    while(at<size){
        while(at<size&&whitespace(p[at]))at++;if(at==size)break;
        int option=prefix_fold(p+at,size-at,"-project=");
        if(option){at+=9;if(at==size||whitespace(p[at]))return -1;}
        int quoted=p[at]=='"';if(quoted)at++;size_t begin=at;
        while(at<size&&(quoted?p[at]!='"':!whitespace(p[at]))){if(!quoted&&p[at]=='"')return -1;at++;}
        size_t n=at-begin;if(!n)return -1;
        if(quoted){if(at==size)return -1;at++;if(at<size&&!whitespace(p[at]))return -1;}
        if(n>=9&&!memcmp(p+begin+n-9,".uproject",9)){
            if(found++||n>=sizeof result||n<=9||memcmp(p+begin,"../../../",9))return -1;
            memcpy(result,p+begin,n);result[n]=0;
            const char *relative=result+9;size_t length=n-9;
            if(length>240||bw_manifest_validate_path(relative,length)||strpbrk(relative,"~%"))return -1;
            const char *slash=strchr(relative,'/');size_t root=slash?(size_t)(slash-relative):length;
            char component[241];memcpy(component,relative,root);component[root]=0;
            if(equal_fold(component,"sce_sys")||equal_fold(component,".blackwire"))return -1;
        }else{
            /* Explicit project options require a supported path. Unknown
             * arguments must not conceal another project request in quotes,
             * an assignment or a differently cased/extended filename. */
            if(option)return -1;
            for(size_t i=0;i+9<=n;i++)if(prefix_fold(p+begin+i,n-i,".uproject"))return -1;
        }
    }
    if(!found)return 0;strcpy(request,result);strcpy(destination,result+9);return 1;
}

struct collected {uint8_t bytes[BW_NATIVE_PROJECT_COMMAND_LIMIT];size_t used,limit;};
static int collect(void *opaque,const void *bytes,size_t size){struct collected *c=opaque;if(size>c->limit-c->used)return -1;memcpy(c->bytes+c->used,bytes,size);c->used+=size;return 0;}
static int command_name(const char *path){const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;return equal_fold(leaf,"ue4commandline.txt")||equal_fold(leaf,"uecommandline.txt")||equal_fold(leaf,"commandline.txt");}
static int same_project(const struct bw_pp_plan *a,const struct bw_pp_plan *b){
    return !strcmp(a->requested,b->requested)&&!strcmp(a->listed,b->listed)&&!strcmp(a->proposed_destination,b->proposed_destination)&&
        a->spans[BW_PP_BODY].size==b->spans[BW_PP_BODY].size&&!memcmp(a->spans[BW_PP_BODY].sha256,b->spans[BW_PP_BODY].sha256,32)&&
        a->module_count==b->module_count&&!memcmp(a->modules,b->modules,sizeof a->modules);
}
int bw_native_project_recipe_open(struct bw_native_project_recipe *p,struct bw_source_provider *provider,int (*current)(void *),void *job){
    if(!p||p->found||p->project.state||p->error)return -1;
    if(!provider||!current||provider->identity.kind!=BW_PROVIDER_FOLDER||provider->identity.policy!=BW_PROVIDER_POLICY_V1||
       bw_source_profile_name(provider->identity.binding.title))return 0;
    if(current(job)||bw_source_provider_current(provider))return fail(p,"project_source_changed");
    struct bw_manifest map;if(bw_manifest_open(&map,provider->bundle.map.bytes,provider->bundle.map.size))return fail(p,"project_map_invalid");
    unsigned commands=0;uint64_t initial_bytes=provider->source->content_bytes;uint32_t initial_requests=provider->source->requests;
    struct collected *text=calloc(1,sizeof *text);if(!text)return fail(p,"project_command_allocation");
    int result=-1;
    for(uint32_t i=0;i<map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))goto done;
        if(!command_name(row.path))continue;
        if(++commands>8||row.flags!=BW_MANIFEST_LOCAL||!row.size||row.size>BW_NATIVE_PROJECT_COMMAND_LIMIT||row.path_length>240){fail(p,"project_command_policy_limit");goto done;}
        memset(text,0,sizeof *text);text->limit=(size_t)row.size;
        if(current(job)||bw_source_provider_read(provider,row.path,collect,text)||text->used!=row.size){fail(p,"project_command_read_failed");goto done;}
        uint8_t hash[32];cp_sha_bytes(text->bytes,text->used,hash);
        if(memcmp(hash,provider->bundle.digests+56u+32u*i,32)){fail(p,"project_command_digest_changed");goto done;}
        char requested[250],destination[241];int parsed=bw_native_project_command(text->bytes,text->used,requested,destination);
        if(parsed<0){fail(p,"project_command_not_supported");goto done;}
        if(!parsed)continue;
        if(p->requested[0]&&strcmp(p->requested,requested)){fail(p,"project_commands_conflict");goto done;}
        if(!p->requested[0]){strcpy(p->requested,requested);strcpy(p->destination,destination);strcpy(p->command_path,row.path);memcpy(p->command_sha256,hash,32);}
    }
    if(!p->requested[0]){result=0;goto done;}
    /* A source that already supplies its exact requested descriptor as a
     * LOCAL startup file needs no derived publication or package lookup. */
    for(uint32_t i=0;i<map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))goto done;
        if(!strcmp(row.path,p->destination)&&row.flags==BW_MANIFEST_LOCAL&&row.size){result=0;goto done;}
    }
    if(map.count>4096||provider->folder.snapshot.count>4097){fail(p,"project_inventory_policy_limit");goto done;}
    unsigned archives=0;
    for(uint32_t i=0;i<map.count;i++){
        struct bw_manifest_record row;if(bw_manifest_get(&map,i,&row))goto done;
        if(!suffix(row.path,".pak"))continue;
        if(++archives>BW_NATIVE_PROJECT_ARCHIVE_LIMIT||row.flags!=BW_MANIFEST_STREAM||row.path_length>240){fail(p,"project_archive_policy_limit");goto done;}
        if(current(job)||provider->source->content_bytes-initial_bytes>UINT64_C(268435456)||provider->source->requests-initial_requests>32768){fail(p,"project_discovery_work_limit");goto done;}
        struct bw_pp_request request={.source=provider->source,.snapshot=&provider->folder.snapshot,.current=current,.job=job,
            .source_kind=BW_PROVIDER_FOLDER,.source_policy=BW_PROVIDER_POLICY_V1,.profile="none",.archive=row.path,
            .requested=p->requested,.listed=p->requested,.proposed_destination=p->destination,
            .base_local_bytes=provider->bundle.map.local_bytes,.startup_budget=provider->budget};
        struct bw_packed_project other={0};struct bw_packed_project *candidate=p->found?&other:&p->project;
        int opened=bw_packed_project_open(candidate,&request);
        if(opened){
            const char *error=candidate->error;
            int absent=error&&(!strcmp(error,"packed_exact_filename_count_location")||!strcmp(error,"packed_unique_plain_v11_footer")||
                !strcmp(error,"packed_footer_size")||!strcmp(error,"packed_filename_index_missing")||!strcmp(error,"packed_empty_archive"));
            if(bw_packed_project_destroy(candidate)||!absent||provider->source->error||!provider->source->smb||current(job)){
                fail(p,error?error:"project_archive_read_failed");goto done;
            }
            continue;
        }
        const struct bw_pp_plan *found=bw_packed_project_view(candidate);
        if(!found){bw_packed_project_destroy(candidate);fail(p,"project_parser_view_failed");goto done;}
        if(p->found){const struct bw_pp_plan *first=bw_packed_project_view(&p->project);int same=first&&same_project(first,found);if(bw_packed_project_destroy(&other)||!same){fail(p,"project_archive_versions_conflict");goto done;}}
        else p->found=1;
    }
    if(!p->found){fail(p,"project_descriptor_not_found_in_supported_paks");goto done;}
    if(provider->source->content_bytes-initial_bytes>UINT64_C(268435456)||provider->source->requests-initial_requests>32768||
       current(job)||bw_source_provider_current(provider)||bw_packed_project_revalidate(&p->project)){
        fail(p,"project_discovery_final_validation_failed");goto done;
    }
    result=1;
done:
    free(text);if(result<0){bw_packed_project_destroy(&p->project);p->found=0;if(!p->error)p->error="project_discovery_failed";}return result;
}
int bw_native_project_recipe_destroy(struct bw_native_project_recipe *p){if(!p)return -1;if(bw_packed_project_destroy(&p->project))return -1;memset(p,0,sizeof *p);return 0;}
