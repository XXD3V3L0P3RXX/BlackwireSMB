#ifndef BW_PACKED_BOOTSTRAP_RULES_H
#define BW_PACKED_BOOTSTRAP_RULES_H
#include <string.h>

#define BW_PBB_ENGINE_PREFIX "Engine/"
#define BW_PBB_UNREAL_STARTUP_PREFIX "UnrealStartup/"
#define BW_PBB_UNREAL_CANONICAL_PREFIX "UnrealCanonical/"
#define BW_PBB_UNREAL_METADATA_PREFIX "UnrealStartupMetadata/"
#define BW_PBB_CURRENT_MODE 5u
#define BW_PBB_CURRENT_PREFIX BW_PBB_UNREAL_METADATA_PREFIX
#define BW_PBB_ENGINE_SELECTOR_SEMANTICS "ICU subtree Engine/Content/Internationalization/ OR literal Engine/GlobalShaderCache-[A-Za-z0-9_]{1,64}.bin"
#define BW_PBB_UNREAL_STARTUP_SELECTOR_SEMANTICS "Engine union OR one safe project root/Content/ShaderArchive-Global-[A-Za-z0-9_]{1,64}.ushaderbytecode; unique-existing-snapshot-directory-case-v1 before mapping; exact leaf; retain literal archive path and entry location; refuse multiple listed project roots, directory aliases, ambiguous or file ancestors and destination conflicts"
#define BW_PBB_UNREAL_CANONICAL_SELECTOR_SEMANTICS "Engine union OR one safe literal project root/Content/ShaderArchive-Global-[A-Za-z0-9_]{1,64}.ushaderbytecode; preserve canonical listed root and Content directory case; validate unique existing snapshot directory ancestors without substituting their spelling; exact leaf; retain literal archive path and entry location; refuse multiple listed project roots, directory aliases, ambiguous or file ancestors and destination conflicts"
#define BW_PBB_UNREAL_METADATA_SELECTOR_SEMANTICS "canonical mode4 loose-body selection plus seed every admitted nonzero original plain-v11 footer/primary/path-hash/filename span at its literal STREAM archive offset; original per-span SHA1/SHA256 and source identity retained; overlap counted per span; aggregate span bytes<=16MiB and within captured startup budget; emit each span once before single final source/body verification; no other Pak bytes or native visibility authority"

/* Pure selector for already addressable, NUL-terminated ASCII path strings.
 * It performs no source, archive, body, ownership or freshness validation. */
static inline unsigned bw_pbb_prefix_mode(const char *prefix){
    if(!prefix)return 0;
    if(!strcmp(prefix,"Engine/Content/Internationalization/"))return 1;
    if(!strcmp(prefix,BW_PBB_ENGINE_PREFIX))return 2u;
    if(!strcmp(prefix,BW_PBB_UNREAL_STARTUP_PREFIX))return 3u;
    if(!strcmp(prefix,BW_PBB_UNREAL_CANONICAL_PREFIX))return 4u;
    return !strcmp(prefix,BW_PBB_UNREAL_METADATA_PREFIX)?5u:0u;
}
static inline unsigned bw_pbb_selector_fold(unsigned c){return c>='A'&&c<='Z'?c+32:c;}
static inline int bw_pbb_selector_prefix(const char *path,const char *prefix){
    while(*prefix){if(!*path||bw_pbb_selector_fold((unsigned char)*path++)!=bw_pbb_selector_fold((unsigned char)*prefix++))return 0;}return 1;
}
static inline int bw_pbb_selector_token(const char *token,const char *suffix){
    unsigned n=0;while(token[n]&&token[n]!='.'){
        unsigned c=(unsigned char)token[n];
        if(n==64||!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'))return -1;n++;
    }return n&&!strcmp(token+n,suffix)?1:-1;
}
/* Directory spelling may already have been mapped through the exact source
 * snapshot. This pure shape check cannot establish that mapping or freshness. */
static inline int bw_pbb_selector_library_match(const char *destination){
    if(!destination)return -1;const char *slash=strchr(destination,'/');
    if(!slash||slash==destination||!bw_pbb_selector_prefix(slash+1,"Content/"))return 0;
    const char *leaf=slash+1+sizeof("Content/")-1;static const char library[]="ShaderArchive-Global-";
    if(!bw_pbb_selector_prefix(leaf,library))return 0;
    if(strncmp(leaf,library,sizeof library-1))return -1;
    size_t root=(size_t)(slash-destination);if(root>240||(root==1&&destination[0]=='.')||(root==2&&!memcmp(destination,"..",2)))return -1;
    for(size_t i=0;i<root;i++){unsigned c=(unsigned char)destination[i];if(c<32||c>=127||strchr("\\:*?\"<>|~%",(int)c))return -1;}
    if(destination[root-1]==' '||destination[root-1]=='.')return -1;
    size_t stem=0;while(stem<root&&destination[stem]!='.')stem++;
    if(stem<=4){char word[5]={0};for(size_t i=0;i<stem;i++)word[i]=(char)bw_pbb_selector_fold((unsigned char)destination[i]);
        if(!strcmp(word,"con")||!strcmp(word,"prn")||!strcmp(word,"aux")||!strcmp(word,"nul")||(stem==4&&(!memcmp(word,"com",3)||!memcmp(word,"lpt",3))&&word[3]>='1'&&word[3]<='9'))return -1;
    }
    return bw_pbb_selector_token(leaf+sizeof library-1,".ushaderbytecode");
}
/* 1=selected, 0=unrelated, -1=unsupported declaration/case alias or malformed
 * recognized shader leaf. Old ICU/Engine declarations keep exact semantics.
 * Declarations are explicit unions, never recursive wildcard selections. */
static inline int bw_pbb_selector_match(const char *prefix,const char *destination){
    unsigned mode=bw_pbb_prefix_mode(prefix);if(!mode||!destination)return -1;
    static const char icu[]="Engine/Content/Internationalization/";
    if(bw_pbb_selector_prefix(destination,icu))return strncmp(destination,icu,sizeof icu-1)?-1:1;
    if(mode==1)return 0;
    static const char shader[]="Engine/GlobalShaderCache-";
    if(bw_pbb_selector_prefix(destination,shader)){
        if(strncmp(destination,shader,sizeof shader-1))return -1;
        return bw_pbb_selector_token(destination+sizeof shader-1,".bin");
    }
    if(mode<3)return 0;
    int selected=bw_pbb_selector_library_match(destination);
    if(mode>=4&&selected==1){const char *slash=strchr(destination,'/');if(strncmp(slash+1,"Content/",sizeof("Content/")-1))return -1;}
    return selected;
}
#endif
