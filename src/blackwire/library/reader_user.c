#include "reader_user.h"
#include <string.h>
int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(uint32_t *);
int bw_reader_foreground_user(uint32_t *user,struct bw_reader_user_observation *observation){
    if(!user||!observation)return BW_READER_USER_FOREGROUND_FAILED;
    memset(observation,0,sizeof *observation);*user=UINT32_MAX;
    observation->foreground_before=sceUserServiceGetForegroundUser(user);
    if(observation->foreground_before==0){
        if(*user!=UINT32_MAX)return BW_READER_USER_OK;
        return BW_READER_USER_FOREGROUND_FAILED;
    }
    *user=UINT32_MAX;observation->initialize_called=1;
    observation->initialize_result=sceUserServiceInitialize(0);
    if(observation->initialize_result!=0)return BW_READER_USER_INIT_FAILED;
    observation->foreground_after_called=1;
    observation->foreground_after=sceUserServiceGetForegroundUser(user);
    if(observation->foreground_after!=0||*user==UINT32_MAX){*user=UINT32_MAX;return BW_READER_USER_FOREGROUND_FAILED;}
    return BW_READER_USER_OK;
}
