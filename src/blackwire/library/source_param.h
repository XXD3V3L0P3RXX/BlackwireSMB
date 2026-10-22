#ifndef BW_SOURCE_PARAM_H
#define BW_SOURCE_PARAM_H
#include <stddef.h>
#define BW_SOURCE_PARAM_LIMIT 65536u
/* Validate a bounded UTF-8 JSON document and extract its single root titleId.
 * Nested sharing IDs are neither selected nor modified. No filesystem calls. */
int bw_source_param_title(const void *,size_t,char title[10]);
/* Same validation, plus optional bounded display text: usable root titleName,
 * then localizedParameters' declared default locale, then its first usable
 * locale. Missing/unusable display text stays empty for the source-name fallback.
 * The title-only and registration identity APIs retain their existing behavior. */
int bw_source_param_metadata(const void *,size_t,char title[10],char *,size_t);
/* Registration identity: require one usable root contentId as well as titleId.
 * This does not change the title/display APIs' treatment of other fields. */
int bw_source_param_identity(const void *,size_t,char title[10],char content[128]);
#endif
