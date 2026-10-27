/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual native registration observations over isolated filesystem and SQLite.
 * The retained lifecycle fixture provides only the normal SMP transport model.
 * Open/close faults below wrap the actual operation; no state/DB stub is used. */
#include "registration_observation_fixture.h"

static char denied_path[P];
static unsigned db_close_fault,db_interrupt_fault,db_finalize_fault;
int registration_observation_open(const char *p,int flags,...){
    mode_t mode=0;if(flags&O_CREAT){va_list v;va_start(v,flags);mode=(mode_t)va_arg(v,int);va_end(v);}
    if(*denied_path&&!strcmp(p,denied_path)){errno=EACCES;return -1;}
    return open(p,flags,mode);
}
int registration_observation_db_close(sqlite3 *db){
    int rc=sqlite3_close(db);if(rc==SQLITE_OK&&db_close_fault){db_close_fault--;return SQLITE_IOERR;}return rc;
}
static int interrupt_query(void *unused){(void)unused;return 1;}
int registration_observation_db_step(sqlite3_stmt *statement){
    if(db_interrupt_fault){db_interrupt_fault--;sqlite3_progress_handler(sqlite3_db_handle(statement),1,interrupt_query,NULL);}
    return sqlite3_step(statement);
}
int registration_observation_db_finalize(sqlite3_stmt *statement){
    int rc=sqlite3_finalize(statement);if(db_finalize_fault){db_finalize_fault--;return SQLITE_IOERR;}return rc;
}
static struct bw_native_registration_observation observe(struct fixture *f,enum bw_registration_state expected,
    enum bw_registration_reason reason,enum bw_registration_reason first){
    unsigned char before[3][32],after[3][32];char p[P];
    path(p,f->source,"eboot.bin");hash_file(p,before[0]);hash_file(f->receipt,before[1]);hash_file(f->db,before[2]);
    struct bw_native_registration_observation o;
    CHECK(bw_native_registration_observe(f->data,f->apps,f->db,&f->selected,&o)==expected);
    CHECK(o.state==expected&&o.reason==reason&&o.first_reason==first);
    path(p,f->source,"eboot.bin");hash_file(p,after[0]);hash_file(f->receipt,after[1]);hash_file(f->db,after[2]);
    CHECK(!memcmp(before,after,sizeof before));return o;
}
static void same_legacy_state(struct fixture *f,enum bw_registration_state expected){
    CHECK(bw_native_registration_state(f->data,f->apps,f->db,&f->selected)==expected);
    CHECK((bw_native_registration_verify(f->data,f->apps,f->db,&f->selected)==0)==(expected==BW_REG_MATCH));
}
static void observation_cases(void){
    struct fixture f;struct bw_native_registration_observation o;char p[P],q[P];
    init(&f,"absent-app-and-db");o=observe(&f,BW_REG_ABSENT,BW_REG_TITLE_ABSENT,BW_REG_APP_DIRECTORY_MISSING);
    CHECK(o.database.state==BW_REG_DB_ABSENT&&o.database.reason==BW_REG_DB_ROW_ABSENT&&o.first_error_snapshot==ENOENT);same_legacy_state(&f,BW_REG_ABSENT);
    init(&f,"exact-registration");registered(&f);o=observe(&f,BW_REG_MATCH,BW_REG_EXACT_MATCH,BW_REG_NOT_OBSERVED);
    CHECK(o.database.state==BW_REG_DB_MATCH&&o.database.reason==BW_REG_DB_ROW_MATCH);same_legacy_state(&f,BW_REG_MATCH);
    init(&f,"db-row-without-app");row(&f,f.content);o=observe(&f,BW_REG_CONFLICT,BW_REG_DATABASE_ROW_WITHOUT_APP,BW_REG_APP_DIRECTORY_MISSING);
    CHECK(o.database.state==BW_REG_DB_MATCH);same_legacy_state(&f,BW_REG_CONFLICT);
    init(&f,"conflicting-db-content");registered(&f);sql(&f,"UPDATE tbl_contentinfo SET contentId='foreign-content' WHERE titleId='PPSA10001';");
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_CONFLICT,BW_REG_DATABASE_CONFLICT);
    CHECK(o.database.state==BW_REG_DB_CONFLICT&&o.database.reason==BW_REG_DB_CONTENT_DIFFERS&&o.database.sqlite_result==SQLITE_ROW&&!o.database.release_or_identity_unknown);same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"duplicate-db-row");registered(&f);row(&f,f.content);o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_CONFLICT,BW_REG_DATABASE_CONFLICT);
    CHECK(o.database.state==BW_REG_DB_CONFLICT&&o.database.reason==BW_REG_DB_DUPLICATE_ROW);same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"residual-icon-only-app");make(f.app,0755);path(p,f.app,"sce_sys");make(p,0755);path(p,f.app,"sce_sys/icon0.png");file(p,"retained icon",13,0644);
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_MOUNT_MISSING,BW_REG_MOUNT_MISSING);
    CHECK(o.error_snapshot==ENOENT&&o.database.state==BW_REG_DB_ABSENT);same_legacy_state(&f,BW_REG_UNKNOWN);CHECK(!lstat(p,&(struct stat){0}));
    init(&f,"wrong-mount");registered(&f);path(p,f.app,"mount.lnk");replace(p,"/different/game",15,0644);
    o=observe(&f,BW_REG_CONFLICT,BW_REG_MOUNT_DIFFERS,BW_REG_MOUNT_DIFFERS);CHECK(o.database.state==BW_REG_DB_MATCH);same_legacy_state(&f,BW_REG_CONFLICT);
    init(&f,"source-param-access-denied");registered(&f);path(denied_path,f.source,"sce_sys/param.json");
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_SOURCE_PARAM_UNREADABLE,BW_REG_SOURCE_PARAM_UNREADABLE);
    CHECK(o.error_snapshot==EACCES&&o.first_error_snapshot==EACCES);same_legacy_state(&f,BW_REG_UNKNOWN);*denied_path=0;
    init(&f,"source-param-invalid");registered(&f);path(p,f.source,"sce_sys/param.json");replace(p,"{}",2,0644);
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_SOURCE_PARAM_INVALID,BW_REG_PARAM_PAIR_DIFFERS);same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"missing-app-param-is-old-pending");registered(&f);path(p,f.app,"sce_sys/param.json");CHECK(!unlink(p));
    o=observe(&f,BW_REG_PENDING,BW_REG_EXACT_PARTIAL,BW_REG_APP_PARAM_UNREADABLE);CHECK(o.database.state==BW_REG_DB_MATCH);same_legacy_state(&f,BW_REG_PENDING);
    init(&f,"malformed-sqlite");registered(&f);replace(f.db,"not a sqlite database",21,0644);
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_UNKNOWN);
    CHECK(o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_PREPARE_FAILED&&o.database.sqlite_result==SQLITE_NOTADB);same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"missing-db-table");registered(&f);sql(&f,"DROP TABLE tbl_contentinfo;");
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_UNKNOWN);
    CHECK(o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_PREPARE_FAILED&&o.database.sqlite_result==SQLITE_ERROR);same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"database-node-not-regular");registered(&f);path(q,f.root,"held-app.db");CHECK(!rename(f.db,q)&&!symlink(q,f.db));
    /* The helper hash intentionally rejects linked fixture DBs. Use the actual
     * observation directly, then check the protected real DB stayed byte exact. */
    unsigned char before[32],after[32];hash_file(q,before);
    CHECK(bw_native_registration_observe(f.data,f.apps,f.db,&f.selected,&o)==BW_REG_UNKNOWN&&o.reason==BW_REG_DATABASE_UNKNOWN);
    CHECK(o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_PATH_INVALID);hash_file(q,after);CHECK(!memcmp(before,after,32));same_legacy_state(&f,BW_REG_UNKNOWN);
    init(&f,"db-close-failure-not-erased-by-fallback");registered(&f);db_close_fault=1;
    o=observe(&f,BW_REG_PENDING,BW_REG_EXACT_PARTIAL,BW_REG_DATABASE_UNKNOWN);
    CHECK(!db_close_fault&&o.first_raw_result==SQLITE_IOERR&&o.database.state==BW_REG_DB_MATCH&&o.database.release_or_identity_unknown);
    init(&f,"db-conflict-with-unknown-close");registered(&f);sql(&f,"UPDATE tbl_contentinfo SET contentId='foreign' WHERE titleId='PPSA10001';");db_close_fault=2;
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_UNKNOWN);
    CHECK(!db_close_fault&&o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_CONTENT_DIFFERS&&o.database.release_or_identity_unknown);
    init(&f,"actual-interrupted-query-is-not-release-unknown");registered(&f);db_interrupt_fault=2;
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_UNKNOWN);
    CHECK(!db_interrupt_fault&&o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_STEP_FAILED&&o.database.sqlite_result==SQLITE_INTERRUPT&&!o.database.release_or_identity_unknown);
    init(&f,"different-finalize-error-retains-release-unknown");registered(&f);db_interrupt_fault=2;db_finalize_fault=2;
    o=observe(&f,BW_REG_UNKNOWN,BW_REG_DATABASE_UNKNOWN,BW_REG_DATABASE_UNKNOWN);
    CHECK(!db_interrupt_fault&&!db_finalize_fault&&o.database.state==BW_REG_DB_UNKNOWN&&o.database.reason==BW_REG_DB_STEP_FAILED&&o.database.sqlite_result==SQLITE_INTERRUPT&&o.database.release_or_identity_unknown);
    init(&f,"standalone-finalize-failure-not-erased");registered(&f);db_finalize_fault=1;
    o=observe(&f,BW_REG_PENDING,BW_REG_EXACT_PARTIAL,BW_REG_DATABASE_UNKNOWN);
    CHECK(!db_finalize_fault&&o.first_raw_result==SQLITE_IOERR&&o.database.state==BW_REG_DB_MATCH&&o.database.release_or_identity_unknown);
    init(&f,"missing-db-path-is-not-absent");registered(&f);path(q,f.root,"saved-app.db");CHECK(!rename(f.db,q));
    CHECK(bw_native_registration_observe(f.data,f.apps,f.db,&f.selected,&o)==BW_REG_UNKNOWN&&o.database.reason==BW_REG_DB_PATH_UNAVAILABLE&&o.database.error_snapshot==ENOENT);same_legacy_state(&f,BW_REG_UNKNOWN);
    cases++;case_name="bounded-fixed-labels-and-invalid-input";
    CHECK(bw_native_registration_observe(NULL,f.apps,f.db,&f.selected,&o)==BW_REG_UNKNOWN&&o.reason==BW_REG_INVALID_ARGUMENT);
    struct bw_registration_db_observation db;
    CHECK(bw_registration_db_observe(NULL,f.selected.title,f.content,&db)<0&&db.state==BW_REG_DB_UNKNOWN&&db.reason==BW_REG_DB_INVALID_ARGUMENT);
    CHECK(!strcmp(bw_registration_state_name((enum bw_registration_state)99),"unknown"));
    CHECK(!strcmp(bw_native_registration_reason_name((enum bw_registration_reason)-1),"unknown"));
    CHECK(!strcmp(bw_registration_db_reason_name((enum bw_registration_db_reason)999),"unknown"));
    CHECK(open_count==close_count);
}
static unsigned changed_at_guard;
static int observed_control(void *opaque,enum bw_nrl_control point){
    struct lifecycle_fixture *t=opaque;int rc=control(opaque,point);
    if(point==BW_NRL_REGISTER&&changed_at_guard&&t->register_guards==changed_at_guard){
        char p[P];make(t->f.app,0755);path(p,t->f.app,"sce_sys");make(p,0755);path(p,t->f.app,"sce_sys/icon0.png");file(p,"retained icon",13,0644);
    }return rc;
}
static void lifecycle_cases(void){
    for(unsigned change=0;change<2;change++){
        struct lifecycle_fixture t;init_lifecycle(&t,change?"between-checks-real-app-appears":"residual-app-actionable-refusal");
        char p[P];if(!change){make(t.f.app,0755);path(p,t.f.app,"sce_sys");make(p,0755);path(p,t.f.app,"sce_sys/icon0.png");file(p,"retained icon",13,0644);}
        else {changed_at_guard=2;t.request.current=observed_control;}
        unsigned char before[3][32],after[3][32];hash_file(t.f.db,before[0]);hash_file(t.f.receipt,before[1]);path(p,t.f.source,"eboot.bin");hash_file(p,before[2]);
        struct bw_nrc_result r;CHECK(bw_nrl_begin(&t.request,&t.owner,&r)<0&&!t.owner&&r.state==BW_NRC_UNKNOWN);
        changed_at_guard=0;CHECK(r.registration_observations==3&&r.registration_after.state==BW_REG_UNKNOWN&&r.registration_after.reason==BW_REG_MOUNT_MISSING);
        CHECK(r.registration_before.state==(change?BW_REG_ABSENT:BW_REG_UNKNOWN));
        CHECK(!strcmp(r.stage,"registration-owner")&&strstr(r.registration_message,change?"changed during":"no mount.lnk")&&strstr(r.registration_message,"Before=")&&strstr(r.registration_message,"after=")&&strstr(r.registration_message,"app_mount_missing"));
        CHECK(!strstr(r.registration_message,t.f.root)&&!strstr(r.registration_message,t.f.content)&&strlen(r.registration_message)<sizeof r.registration_message);
        CHECK(!t.removes&&!t.uninstall_calls&&!t.unmount_calls&&!t.observations);
        no_pending(&t.f);path(p,t.f.app,"mount.lnk");CHECK(lstat(p,&(struct stat){0})<0&&errno==ENOENT);
        hash_file(t.f.db,after[0]);hash_file(t.f.receipt,after[1]);path(p,t.f.source,"eboot.bin");hash_file(p,after[2]);CHECK(!memcmp(before,after,sizeof before));finish(&t);
    }
}
int main(int argc,char **argv){
    program=argv[0];case_name="startup";CHECK(argc==2&&strlen(argv[1])<P);strcpy(suite,argv[1]);make(suite,0700);
    observation_cases();lifecycle_cases();
    printf("{\"passed\":true,\"cases\":%u,\"checks\":%u,\"opens\":%u,\"closes\":%u,\"actualNativeObservation\":true,\"realFilesystem\":true,\"readonlySqlite\":true,\"normalSmpTransportModeled\":true,\"consoleActions\":0}\n",cases,checks,open_count,close_count);return 0;
}
