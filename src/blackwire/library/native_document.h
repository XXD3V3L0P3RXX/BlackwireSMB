#ifndef BW_NATIVE_DOCUMENT_H
#define BW_NATIVE_DOCUMENT_H
#include <stddef.h>
#define BW_NATIVE_DOCUMENT_LIMIT (1024u*1024u)
/* Bounded strict JSON using the same UTF-8/string/number/depth parser as game
 * metadata. Slices are borrowed for this synchronous call. A callback must not
 * retain them. A long root key is truncated in key; key_size is the full size. */
typedef int (*bw_document_field_fn)(void *,const char *,size_t,const void *,size_t);
typedef int (*bw_document_item_fn)(void *,const void *,size_t);
int bw_document_fields(const void *,size_t,bw_document_field_fn,void *);
int bw_document_array(const void *,size_t,bw_document_item_fn,void *);
int bw_document_string(const void *,size_t,char *,size_t);
#endif
