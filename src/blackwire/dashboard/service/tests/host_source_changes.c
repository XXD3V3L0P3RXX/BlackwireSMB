/* SPDX-License-Identifier: GPL-3.0-or-later
 * Actual filesystem/SQLite observations with one deliberate host-only mutation
 * after the first successful registration lookup. No console or network. */
#define _XOPEN_SOURCE 700
#include "catalog_adapter.h"
#include <sqlite3.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *mode,*source,*apps,*database;
static unsigned changes,queries;
int __real_bw_registration_db_match(const char *,const char *,const char *);
static void alter(const char *path){
    int fd=open(path,O_RDWR|O_NOFOLLOW);if(fd<0)abort();
    unsigned char byte=0;if(pread(fd,&byte,1,0)!=1)abort();byte^=1;
    if(pwrite(fd,&byte,1,0)!=1||fsync(fd)||close(fd))abort();changes++;
}
int __wrap_bw_registration_db_match(const char *path,const char *title,const char *content){
    int result=__real_bw_registration_db_match(path,title,content);queries++;
    if(queries==1&&result==1&&strcmp(mode,"stable")){
        char target[1024];
        if(!strcmp(mode,"boot"))snprintf(target,sizeof target,"%s/eboot.bin",source);
        else if(!strcmp(mode,"source-param"))snprintf(target,sizeof target,"%s/sce_sys/param.json",source);
        else if(!strcmp(mode,"registered-param"))snprintf(target,sizeof target,"%s/%s/sce_sys/param.json",apps,title);
        else if(!strcmp(mode,"mount-link"))snprintf(target,sizeof target,"%s/%s/mount.lnk",apps,title);
        else if(!strcmp(mode,"database")){
            sqlite3 *db=NULL;if(sqlite3_open(database,&db)!=SQLITE_OK)abort();
            if(sqlite3_exec(db,"UPDATE tbl_contentinfo SET contentId='changed-after-observation'",NULL,NULL,NULL)!=SQLITE_OK||sqlite3_close(db))abort();
            changes++;return result;
        }else abort();
        alter(target);
    }
    return result;
}
int main(int argc,char **argv){
    if(argc!=6)return 2;mode=argv[1];apps=argv[3];database=argv[4];
    FILE *file=fopen(argv[2],"rb");if(!file)return 3;
    unsigned char raw[65536];size_t size=fread(raw,1,sizeof raw,file);if(ferror(file)||fclose(file))return 4;
    struct bd_smp_record row;size_t count=0;
    if(bd_smp_records_parse(raw,size,&row,1,&count)||count!=1)return 5;source=row.source;
    struct bd_local_environment environment={apps,database,(unsigned)(!strcmp(argv[5],"allowed")),""};
    struct bd_local_observation observed;int result=bd_local_observe(&row,&environment,&observed);
    struct bd_item item;struct bd_catalog catalog;unsigned flags=0;
    if(!result&&!bd_catalog_init(&catalog,&item,1,1)&&!bd_catalog_add_local(&catalog,&observed.item))flags=item.flags;
    printf("{\"observed\":%d,\"flags\":%u,\"metadataVerified\":%u,\"allAssetsHashed\":%u,\"exclusions\":%u,\"changes\":%u,\"queries\":%u}\n",
        result,flags,observed.metadata_verified,observed.all_assets_hashed,observed.item.exclusions,changes,queries);
    return 0;
}
