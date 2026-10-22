#ifndef BW_REGISTRATION_PARAM_COMPAT_H
#define BW_REGISTRATION_PARAM_COMPAT_H
#include <stddef.h>
enum bw_registration_param_match {
    BW_REGISTRATION_PARAM_REFUSE = -1,
    BW_REGISTRATION_PARAM_EXACT = 0,
    BW_REGISTRATION_PARAM_SHADOWMOUNT_DRM = 1
};
/* Both documents are strict bounded JSON with unique decoded keys per object
 * and identical root title/content identities. Accept exact bytes, or only the
 * pinned ShadowMount first-raw upgradable->standard transformation when that
 * occurrence is the complete literal value of root applicationDrmType.
 * No filesystem, registration, source write, normalization, or launch occurs.
 */
enum bw_registration_param_match bw_registration_param_pair(const void *,size_t,const void *,size_t);
#endif
