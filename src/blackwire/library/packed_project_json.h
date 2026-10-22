#ifndef BW_PACKED_PROJECT_JSON_H
#define BW_PACKED_PROJECT_JSON_H
#include <stddef.h>
/* Internal pure DATA parser; zero success. UTF-8 BOM, decoded duplicate keys,
 * bounded finite floats/integers and Python-compatible escaped surrogates.
 * Emits only schema-validated ASCII module names, never arbitrary JSON. */
int bw_pp_json(const void *,size_t,char modules[32][65],unsigned *count,int (*current)(void *),void *);
#endif
