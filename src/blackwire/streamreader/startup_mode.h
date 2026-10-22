#ifndef BW_STARTUP_MODE_H
#define BW_STARTUP_MODE_H
#include <string.h>

/* Console loader permissions come from the selected role/path, not Windows'
 * synthesized executable bits. Opaque game assets do not become executables.
 */
static inline unsigned bw_startup_mode(const char *path) {
    if (!path) return 0644;
    const char *leaf=strrchr(path,'/');leaf=leaf?leaf+1:path;
    if (!strcmp(path,"eboot.bin")) return 0755;
    const char *extension=strrchr(leaf,'.');
    if (!extension || strlen(extension)>5) return 0644;
    char lower[6];size_t size=strlen(extension);
    for (size_t i=0;i<=size;i++) {
        unsigned char c=(unsigned char)extension[i];
        lower[i]=(char)(c>='A'&&c<='Z'?c+32:c);
    }
    return !strcmp(lower,".prx") || !strcmp(lower,".sprx") ||
        !strcmp(lower,".self") || !strcmp(lower,".elf")?0755:0644;
}
#endif
