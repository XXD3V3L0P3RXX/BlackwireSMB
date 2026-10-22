/* SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise the actual startup guard and private-file installer. Sony
 * registration and process observations are explicitly not exercised. */
#define _XOPEN_SOURCE 700
#include "../../package/native_publish.c"

static int backend_state,sony_state;
static unsigned checks;
#define CHECK(value) do{checks++;if(!(value)){fprintf(stderr,"publish check%d failed: %s\n",__LINE__,#value);return 90;}}while(0)
int bw_native_backend_busy(struct bw_native_backend *backend){return backend?backend_state:-1;}
int bw_native_setup_idle(void *opaque){(void)opaque;return sony_state;}
int main(int argc,char **argv){
    if(argc!=3)return 2;int dummy=0;
    struct publish context={.backend=(struct bw_native_backend *)&dummy};
    CHECK(startup_idle(NULL)<0);CHECK(startup_idle(&(struct publish){0})<0);
    backend_state=1;CHECK(startup_idle(&context)<0);backend_state=-1;CHECK(startup_idle(&context)<0);
    backend_state=0;sony_state=1;CHECK(startup_idle(&context)<0);sony_state=-1;CHECK(startup_idle(&context)<0);
    sony_state=0;CHECK(!startup_idle(&context));
    const unsigned char boot[]="native-installer-data-fixture";
    const unsigned char param[]="{\"titleId\":\"PPSA99171\",\"contentId\":\"EP0000-PPSA99171_00-BLACKWIREDASH0001\"}";
    unsigned char compressed[2][512];struct bd_asset assets[2]={0};
    const unsigned char *raw[]={boot,param};const size_t sizes[]={sizeof boot-1,sizeof param-1};
    for(unsigned i=0;i<2;i++){
        int size=LZ4_compress_default((const char *)raw[i],(char *)compressed[i],(int)sizes[i],sizeof compressed[i]);CHECK(size>0);
        assets[i]=(struct bd_asset){i?"sce_sys/param.json":"eboot.bin",compressed[i],(uint32_t)size,(uint32_t)sizes[i],i?0644u:0755u,{0}};
        cp_sha_bytes(raw[i],sizes[i],assets[i].sha256);
    }
    struct bd_native_bundle bundle={assets,2,{1}};struct bd_install_result installed;
    CHECK(!startup_idle(&context));CHECK(!bd_native_install(&bundle,argv[1],argv[2],&installed));
    CHECK(installed.installed&&installed.verified_files==2&&!installed.source_conflict);
    CHECK(!bd_native_install(&bundle,argv[1],argv[2],&installed)&&installed.already_current);
    char path[1024];CHECK(snprintf(path,sizeof path,"%s/PPSA99171/eboot.bin",argv[1])>0);
    int fd=open(path,O_WRONLY|O_NOFOLLOW);CHECK(fd>=0);CHECK(pwrite(fd,"x",1,0)==1);CHECK(!close(fd));
    CHECK(bd_native_install(&bundle,argv[1],argv[2],&installed)<0&&installed.source_conflict);
    printf("{\"passed\":true,\"checks\":%u,\"bfpRequired\":false,\"actualPrivateInstaller\":true,\"sonyRegistrationExercised\":false}\n",checks);
    return 0;
}
