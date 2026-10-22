#ifndef BW_NATIVE_PREPARED_TARGET_H
#define BW_NATIVE_PREPARED_TARGET_H
#include "native_derived_send.h"
#include "native_batch_send.h"
#include "reader_selection.h"
int bw_native_prepared_from_derived(const struct bw_native_derived_prepared *,struct bw_native_prepared *);
int bw_native_prepared_from_batch(const struct bw_native_batch_prepared *,struct bw_native_prepared *);
int bw_native_prepared_inspect(const char *,const struct bw_provider_binding *,struct bw_native_prepared *);
/* Canonical validated selection -> distinct target; original identity stays
 * untouched. A path alone never substitutes for exact receipt preflight. */
int bw_native_selected_root(const struct bw_reader_selection *,char[75]);
#endif
