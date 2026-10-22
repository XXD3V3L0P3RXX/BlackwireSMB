/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual default Restore wrapper, actual durable Removed store and readonly
 * SQLite, including guards after the wrapper's own tombstone unlink. */
#define BW_NATIVE_BACKEND_HOST_TEST 1
#define BW_NATIVE_PRELOAD 1
#define BW_NATIVE_PLAYBACK 1
#include "native_backend.c"
#include <sqlite3.h>
#include <stdlib.h>
#include <sys/stat.h>
static unsigned checks,cases,guards;
static int guard_failed;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"restore default case%u line%d: %s errno%d\n",cases,__LINE__,#x,errno);abort();}}while(0)
static char data[2048],games[2048],scope[2048],apps[2048],database[2048];
static struct bw_native_removal_selection selected;
static int guard(void *p){CHECK(p==&selected);guards++;return guard_failed?-1:0;}
static int absent(void *p,const struct bw_removed_source *s){
    CHECK(p==&selected&&s);char path[2048];struct stat st;
    CHECK(snprintf(path,sizeof path,"%s/%s",games,s->identity.binding.game)>0);
    return lstat(path,&st)==0||errno!=ENOENT||bw_registration_db_match(database,s->identity.binding.title,"ABSENCE")!=0?-1:0;
}
static void directory(char *out,const char *root,const char *leaf){CHECK(snprintf(out,2048,"%s/%s",root,leaf)>0&&mkdir(out,0755)==0);}
static void init(const char *root,unsigned serial){
    char base[2048],name[16];CHECK(snprintf(name,sizeof name,"%u",serial)>0);directory(base,root,name);directory(data,base,"data");directory(games,data,"games");directory(scope,data,"native-prepared");directory(apps,base,"apps");CHECK(snprintf(database,sizeof database,"%s/app.db",base)>0);
    sqlite3 *db=NULL;CHECK(sqlite3_open(database,&db)==SQLITE_OK&&sqlite3_exec(db,"CREATE TABLE tbl_contentinfo(titleId TEXT,contentId TEXT);INSERT INTO tbl_contentinfo VALUES('PPSA10000','unrelated');",NULL,NULL,NULL)==SQLITE_OK&&sqlite3_close(db)==SQLITE_OK);
    registration_environment=(struct bw_nrc_environment){data,apps,database};backend_data_root=data;guard_failed=0;
    memset(&selected,0,sizeof selected);selected.identity.kind=BW_PROVIDER_FOLDER;selected.identity.policy=BW_PROVIDER_POLICY_V1;strcpy(selected.identity.binding.source,"Owned Backup");strcpy(selected.identity.binding.title,"PPSA10001");memset(selected.identity.binding.sha256,0xaa,32);
    CHECK(snprintf(selected.identity.binding.game,sizeof selected.identity.binding.game,"%s_",selected.identity.binding.title)==10);memset(selected.identity.binding.game+10,'a',64);strcpy(selected.operation,"000000000000000000000000000000ac");
    struct bw_removed_source removed={.identity=selected.identity};memset(removed.prepared_sha256,0x55,32);strcpy(removed.operation,"000000000000000000000000000000ab");
    struct bw_removed_ops ops={&selected,guard,absent};struct bw_removed_result result;
    CHECK(bw_removed_commit(data,&removed,&ops,&result)==0&&result.state==BW_REMOVED_COMMITTED);cases++;
}
static void still_removed(void){struct bw_removed_source got;struct bw_removed_result state;CHECK(!bw_removed_lookup(data,&selected.identity,&got,&state)&&state.state==BW_REMOVED_COMMITTED);}
int main(int argc,char **argv){
    CHECK(argc==2&&mkdir(argv[1],0700)==0);struct bw_game_uninstall_result out;struct bw_removed_source got;struct bw_removed_result state;char p[2048];
    init(argv[1],1);unsigned before=guards;CHECK(!default_restore(NULL,&selected,guard,&selected,&out)&&out.phase==BW_GAME_REMOVE_DONE&&out.registration_confirmed&&out.source_absent&&out.prepared_record_absent&&!out.library_removed&&guards>before);
    CHECK(!bw_removed_lookup(data,&selected.identity,&got,&state)&&state.state==BW_REMOVED_VISIBLE);
    /* Same consumed Restore re-observes; never needs the just-removed record. */
    CHECK(!default_restore(NULL,&selected,guard,&selected,&out)&&out.phase==BW_GAME_REMOVE_DONE);
    init(argv[1],2);guard_failed=1;CHECK(default_restore(NULL,&selected,guard,&selected,&out)<0&&out.phase==BW_GAME_REMOVE_ERROR&&!out.mutation_started);guard_failed=0;still_removed();
    init(argv[1],3);CHECK(snprintf(p,sizeof p,"%s/registration-owners",data)>0&&mkdir(p,0700)==0);CHECK(default_restore(NULL,&selected,guard,&selected,&out)<0&&out.phase==BW_GAME_REMOVE_ERROR&&!out.mutation_started);still_removed();
    init(argv[1],4);CHECK(rmdir(apps)==0);CHECK(default_restore(NULL,&selected,guard,&selected,&out)<0&&out.phase==BW_GAME_REMOVE_UNKNOWN&&out.mutation_started);still_removed();
    init(argv[1],5);CHECK(snprintf(p,sizeof p,"%s/%s",games,selected.identity.binding.game)>0&&mkdir(p,0755)==0);CHECK(default_restore(NULL,&selected,guard,&selected,&out)<0&&out.phase==BW_GAME_REMOVE_UNKNOWN&&out.mutation_started);still_removed();
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"actualDefaultRestore\":true,\"realFilesystemAndReadonlySqlite\":true,\"consoleActions\":0}\n",cases,checks);return 0;
}
