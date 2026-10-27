#ifndef _XOPEN_SOURCE
#define _XOPEN_SOURCE 700
#endif
#include "native_registration.h"
#include "media_public_trial.h"
#include <sqlite3.h>
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Wrap the native Unix VFS rather than marking the database immutable: the
 * real shared locks are retained, while all mutation/recovery paths fail. */
struct ro_file {sqlite3_file file;sqlite3_file *held;};
static sqlite3_vfs ro_vfs,*underlying;
static pthread_once_t ro_once=PTHREAD_ONCE_INIT;
static int ro_ready;
static struct ro_file *rf(sqlite3_file *f){return (struct ro_file *)f;}
static int rclose(sqlite3_file *f){int rc=rf(f)->held->pMethods->xClose(rf(f)->held);f->pMethods=NULL;return rc;}
static int rread(sqlite3_file *f,void *p,int n,sqlite3_int64 at){return rf(f)->held->pMethods->xRead(rf(f)->held,p,n,at);}
static int rwrite(sqlite3_file *f,const void *p,int n,sqlite3_int64 at){(void)f;(void)p;(void)n;(void)at;return SQLITE_READONLY;}
static int rtruncate(sqlite3_file *f,sqlite3_int64 n){(void)f;(void)n;return SQLITE_READONLY;}
static int rsync(sqlite3_file *f,int flags){(void)f;(void)flags;return SQLITE_READONLY;}
static int rsize(sqlite3_file *f,sqlite3_int64 *n){return rf(f)->held->pMethods->xFileSize(rf(f)->held,n);}
static int rlock(sqlite3_file *f,int lock){return lock>SQLITE_LOCK_SHARED?SQLITE_READONLY:rf(f)->held->pMethods->xLock(rf(f)->held,lock);}
static int runlock(sqlite3_file *f,int lock){return rf(f)->held->pMethods->xUnlock(rf(f)->held,lock);}
static int rreserved(sqlite3_file *f,int *held){return rf(f)->held->pMethods->xCheckReservedLock(rf(f)->held,held);}
static int rcontrol(sqlite3_file *f,int op,void *arg){
    if(op!=SQLITE_FCNTL_LOCKSTATE&&op!=SQLITE_FCNTL_HAS_MOVED&&op!=SQLITE_FCNTL_VFSNAME)return SQLITE_NOTFOUND;
    return rf(f)->held->pMethods->xFileControl(rf(f)->held,op,arg);
}
static int rsector(sqlite3_file *f){return rf(f)->held->pMethods->xSectorSize(rf(f)->held);}
static int rdevice(sqlite3_file *f){return rf(f)->held->pMethods->xDeviceCharacteristics(rf(f)->held);}
static int rshm(sqlite3_file *f,int a,int b,int c,void volatile **out){(void)f;(void)a;(void)b;(void)c;*out=NULL;return SQLITE_READONLY;}
static int rshmlock(sqlite3_file *f,int a,int b,int c){(void)f;(void)a;(void)b;(void)c;return SQLITE_READONLY;}
static void rbarrier(sqlite3_file *f){(void)f;}
static int runmap(sqlite3_file *f,int remove){(void)f;(void)remove;return SQLITE_OK;}
static const sqlite3_io_methods ro_methods={.iVersion=2,.xClose=rclose,.xRead=rread,.xWrite=rwrite,.xTruncate=rtruncate,.xSync=rsync,
    .xFileSize=rsize,.xLock=rlock,.xUnlock=runlock,.xCheckReservedLock=rreserved,.xFileControl=rcontrol,.xSectorSize=rsector,.xDeviceCharacteristics=rdevice,
    .xShmMap=rshm,.xShmLock=rshmlock,.xShmBarrier=rbarrier,.xShmUnmap=runmap};
static int ro_open(sqlite3_vfs *v,const char *name,sqlite3_file *f,int flags,int *out){
    (void)v;memset(f,0,(size_t)ro_vfs.szOsFile);
    if(!name||!(flags&SQLITE_OPEN_READONLY)||(flags&(SQLITE_OPEN_CREATE|SQLITE_OPEN_READWRITE|SQLITE_OPEN_DELETEONCLOSE|SQLITE_OPEN_URI)))return SQLITE_CANTOPEN;
    struct ro_file *r=rf(f);size_t offset=(sizeof *r+15u)&~(size_t)15u;r->held=(sqlite3_file *)((unsigned char *)f+offset);
    int actual=0,rc=underlying->xOpen(underlying,name,r->held,flags|SQLITE_OPEN_NOFOLLOW,&actual);
    if(rc!=SQLITE_OK)return rc;if(!(actual&SQLITE_OPEN_READONLY)){r->held->pMethods->xClose(r->held);return SQLITE_READONLY;}
    f->pMethods=&ro_methods;if(out)*out=actual;return SQLITE_OK;
}
static int ro_delete(sqlite3_vfs *v,const char *name,int sync){(void)v;(void)name;(void)sync;return SQLITE_READONLY;}
static int ro_access(sqlite3_vfs *v,const char *name,int flags,int *out){(void)v;return underlying->xAccess(underlying,name,flags,out);}
static int ro_path(sqlite3_vfs *v,const char *name,int size,char *out){(void)v;return underlying->xFullPathname(underlying,name,size,out);}
static void ro_initialize(void){
    underlying=sqlite3_vfs_find(NULL);if(!underlying)return;ro_vfs=*underlying;
    ro_vfs.iVersion=1;ro_vfs.pNext=NULL;ro_vfs.zName="blackwire-readonly";ro_vfs.pAppData=underlying;
    ro_vfs.szOsFile=(int)(((sizeof(struct ro_file)+15u)&~(size_t)15u)+(size_t)underlying->szOsFile);
    ro_vfs.xOpen=ro_open;ro_vfs.xDelete=ro_delete;ro_vfs.xAccess=ro_access;ro_vfs.xFullPathname=ro_path;
    ro_ready=sqlite3_vfs_register(&ro_vfs,0)==SQLITE_OK;
}
static int authorize(void *p,int op,const char *table,const char *column,const char *db,const char *trigger){
    (void)p;(void)trigger;
    if(op==SQLITE_SELECT)return SQLITE_OK;
    return op==SQLITE_READ&&table&&column&&db&&!strcmp(db,"main")&&!strcmp(table,"tbl_contentinfo")&&
        (!strcmp(column,"titleId")||!strcmp(column,"contentId"))?SQLITE_OK:SQLITE_DENY;
}
static int progress(void *opaque){return ++*(unsigned *)opaque>2000;}
static void db_note(struct bw_registration_db_observation *o,enum bw_registration_db_reason reason,int code,int error){
    if(o->reason==BW_REG_DB_NOT_OBSERVED){o->reason=reason;o->sqlite_result=code;o->error_snapshot=error;}
}
const char *bw_registration_db_state_name(enum bw_registration_db_state state){
    switch(state){case BW_REG_DB_ABSENT:return "absent";case BW_REG_DB_MATCH:return "match";case BW_REG_DB_CONFLICT:return "conflict";default:return "unknown";}
}
const char *bw_registration_db_reason_name(enum bw_registration_db_reason reason){
    static const char *const names[]={"not_observed","invalid_argument","path_unavailable","path_invalid","file_invalid","vfs_unavailable",
        "open_failed","not_readonly","prepare_failed","bind_failed","step_failed","row_invalid","content_differs","duplicate_row",
        "finalize_failed","close_failed","identity_changed","row_absent","row_match"};
    return (unsigned)reason<sizeof names/sizeof names[0]?names[reason]:"unknown";
}
int bw_registration_db_observe(const char *path,const char *title,const char *content,struct bw_registration_db_observation *out){
    struct bw_registration_db_observation observed={.state=BW_REG_DB_UNKNOWN};
    if(!out)out=&observed;else *out=observed;
    if(!path||!title||!content||strlen(title)!=9||!content[0]||strnlen(content,128)>=128){db_note(out,BW_REG_DB_INVALID_ARGUMENT,0,0);return -1;}
    char canonical[PATH_MAX];struct stat before,after;
    if(!realpath(path,canonical)){db_note(out,BW_REG_DB_PATH_UNAVAILABLE,0,errno);return -1;}
    if(strcmp(path,canonical)){db_note(out,BW_REG_DB_PATH_INVALID,0,0);return -1;}
    if(lstat(path,&before)){db_note(out,BW_REG_DB_PATH_UNAVAILABLE,0,errno);return -1;}
    if(!S_ISREG(before.st_mode)||before.st_nlink!=1||before.st_size<=0||before.st_size>128*1024*1024){db_note(out,BW_REG_DB_FILE_INVALID,0,0);return -1;}
    pthread_once(&ro_once,ro_initialize);if(!ro_ready){db_note(out,BW_REG_DB_VFS_UNAVAILABLE,0,0);return -1;}
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;int result=-1,conflict=0,rc,step_error=SQLITE_OK;unsigned steps=0;
    rc=sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX|SQLITE_OPEN_NOFOLLOW,"blackwire-readonly");
    if(rc!=SQLITE_OK){db_note(out,BW_REG_DB_OPEN_FAILED,rc,0);goto done;}
    rc=sqlite3_db_readonly(db,"main");if(rc!=1){db_note(out,BW_REG_DB_NOT_READONLY,rc,0);goto done;}
    /* SQLite parses the complete stored schema before this constant SELECT.
     * The console's unrelated icon table has 88 columns and CREATE statements
     * exceed 1 KiB; narrower limits wrongly make a healthy app.db unreadable. */
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,1024*1024);sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,4096);sqlite3_limit(db,SQLITE_LIMIT_COLUMN,256);
    sqlite3_busy_timeout(db,1000);sqlite3_progress_handler(db,1000,progress,&steps);sqlite3_set_authorizer(db,authorize,NULL);
    rc=sqlite3_prepare_v2(db,"SELECT contentId FROM tbl_contentinfo WHERE titleId=?1 COLLATE BINARY;",-1,&stmt,NULL);
    if(rc!=SQLITE_OK){db_note(out,BW_REG_DB_PREPARE_FAILED,rc,0);goto done;}
    if(!sqlite3_stmt_readonly(stmt)){db_note(out,BW_REG_DB_NOT_READONLY,0,0);goto done;}
    rc=sqlite3_bind_text(stmt,1,title,9,SQLITE_STATIC);if(rc!=SQLITE_OK){db_note(out,BW_REG_DB_BIND_FAILED,rc,0);goto done;}
    rc=sqlite3_step(stmt);if(rc==SQLITE_DONE){result=0;goto done;}
    if(rc!=SQLITE_ROW){step_error=rc;db_note(out,BW_REG_DB_STEP_FAILED,rc,0);goto done;}
    rc=sqlite3_column_type(stmt,0);if(rc!=SQLITE_TEXT){db_note(out,BW_REG_DB_ROW_INVALID,rc,0);goto done;}
    int length=sqlite3_column_bytes(stmt,0);const unsigned char *bytes=sqlite3_column_text(stmt,0);
    if(!bytes){db_note(out,BW_REG_DB_ROW_INVALID,0,0);goto done;}
    if(length!=(int)strlen(content)||memcmp(bytes,content,(size_t)length)){conflict=1;db_note(out,BW_REG_DB_CONTENT_DIFFERS,SQLITE_ROW,0);goto done;}
    rc=sqlite3_step(stmt);if(rc!=SQLITE_DONE){conflict=rc==SQLITE_ROW;if(!conflict)step_error=rc;db_note(out,conflict?BW_REG_DB_DUPLICATE_ROW:BW_REG_DB_STEP_FAILED,rc,0);goto done;}
    result=1;
done:
    /* Finalize consumes the statement even when it echoes the preceding step
     * error. That known query error alone does not imply an uncertain release. */
    if(stmt&&(rc=sqlite3_finalize(stmt))!=SQLITE_OK){db_note(out,BW_REG_DB_FINALIZE_FAILED,rc,0);if(rc!=step_error)out->release_or_identity_unknown=1;result=-1;}
    if(db&&(rc=sqlite3_close(db))!=SQLITE_OK){db_note(out,BW_REG_DB_CLOSE_FAILED,rc,0);out->release_or_identity_unknown=1;result=-1;}
    if(lstat(path,&after)){db_note(out,BW_REG_DB_PATH_UNAVAILABLE,0,errno);out->release_or_identity_unknown=1;result=-1;}
    else if(!S_ISREG(after.st_mode)||after.st_nlink!=1||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino){db_note(out,BW_REG_DB_IDENTITY_CHANGED,0,0);out->release_or_identity_unknown=1;result=-1;}
    else if(!realpath(path,canonical)){db_note(out,BW_REG_DB_PATH_UNAVAILABLE,0,errno);out->release_or_identity_unknown=1;result=-1;}
    else if(strcmp(path,canonical)){db_note(out,BW_REG_DB_IDENTITY_CHANGED,0,0);out->release_or_identity_unknown=1;result=-1;}
    out->state=result==1?BW_REG_DB_MATCH:result==0?BW_REG_DB_ABSENT:conflict&&!out->release_or_identity_unknown?BW_REG_DB_CONFLICT:BW_REG_DB_UNKNOWN;
    if(out->reason==BW_REG_DB_NOT_OBSERVED)out->reason=result==1?BW_REG_DB_ROW_MATCH:BW_REG_DB_ROW_ABSENT;
    return result;
}
int bw_registration_db_match(const char *path,const char *title,const char *content){return bw_registration_db_observe(path,title,content,NULL);}

int bw_registration_db_titles(const char *path,struct bw_registration_titles *out){
    if(!path||!out)return -1;memset(out,0,sizeof *out);
    char canonical[PATH_MAX];struct stat before,after;
    if(!realpath(path,canonical)||strcmp(path,canonical)||lstat(path,&before)||!S_ISREG(before.st_mode)||before.st_nlink!=1||
       before.st_size<=0||before.st_size>128*1024*1024)return -1;
    pthread_once(&ro_once,ro_initialize);if(!ro_ready)return -1;
    sqlite3 *db=NULL;sqlite3_stmt *stmt=NULL;int result=-1;unsigned steps=0;
    if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY|SQLITE_OPEN_NOMUTEX|SQLITE_OPEN_NOFOLLOW,"blackwire-readonly")!=SQLITE_OK||
       sqlite3_db_readonly(db,"main")!=1)goto done;
    sqlite3_limit(db,SQLITE_LIMIT_LENGTH,1024*1024);sqlite3_limit(db,SQLITE_LIMIT_SQL_LENGTH,4096);sqlite3_limit(db,SQLITE_LIMIT_COLUMN,256);
    sqlite3_busy_timeout(db,250);sqlite3_progress_handler(db,1000,progress,&steps);sqlite3_set_authorizer(db,authorize,NULL);
    if(sqlite3_prepare_v2(db,"SELECT titleId FROM tbl_contentinfo WHERE titleId != '' ORDER BY titleId COLLATE BINARY;",-1,&stmt,NULL)!=SQLITE_OK||
       !sqlite3_stmt_readonly(stmt))goto done;
    int rc;
    while((rc=sqlite3_step(stmt))==SQLITE_ROW){
        if(out->count==BW_MEDIA_PUBLIC_DB_LIMIT||sqlite3_column_type(stmt,0)!=SQLITE_TEXT||sqlite3_column_bytes(stmt,0)!=9)goto done;
        const unsigned char *title=sqlite3_column_text(stmt,0);if(!title)goto done;
        for(unsigned i=0;i<9;i++)if(!((title[i]>='A'&&title[i]<='Z')||(title[i]>='0'&&title[i]<='9')))goto done;
        char *slot=out->title[out->count];memcpy(slot,title,9);slot[9]=0;
        if(out->count&&strcmp(out->title[out->count-1],slot)>=0)goto done;out->count++;
    }
    if(rc==SQLITE_DONE)result=0;
done:
    if(stmt&&sqlite3_finalize(stmt)!=SQLITE_OK)result=-1;if(db&&sqlite3_close(db)!=SQLITE_OK)result=-1;
    if(lstat(path,&after)||!S_ISREG(after.st_mode)||after.st_nlink!=1||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino||
       !realpath(path,canonical)||strcmp(path,canonical))result=-1;
    if(result)memset(out,0,sizeof *out);return result;
}
