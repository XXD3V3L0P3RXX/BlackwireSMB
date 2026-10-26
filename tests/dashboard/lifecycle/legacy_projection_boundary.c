/* Strictly policy0 retained ledger fixtures do not contain a source/admission
 * transport model. Any attempt to enter a newly projected branch is a failed
 * test, never a successful witness. The full actual projected producer and
 * ledger chain is qualified separately by check_projected_game_uninstall_e2e.
 */
#include "native_launch_admission.h"
#include <stdlib.h>
int bw_native_send_inspect(const char *a,const struct bw_provider_binding *b,struct bw_native_prepared *c){(void)a;(void)b;(void)c;abort();}
int bw_native_launch_admission_open_for_removal(struct bw_native_launch_admission **a,const char *b,const struct bw_provider_binding *c,const struct bw_native_prepared *d,const char *e,int(*f)(void *),void *g,const char **h){(void)a;(void)b;(void)c;(void)d;(void)e;(void)f;(void)g;(void)h;abort();}
int bw_native_launch_admission_current(struct bw_native_launch_admission *a,const char *b,const struct bw_provider_binding *c,const struct bw_reader_selection *d,struct bw_native_prepared *e){(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
int bw_native_launch_admission_current_for_removal(struct bw_native_launch_admission *a,const char *b,const struct bw_provider_binding *c,const struct bw_reader_selection *d,struct bw_native_prepared *e){(void)a;(void)b;(void)c;(void)d;(void)e;abort();}
int bw_native_launch_admission_companions(struct bw_native_launch_admission *a,const struct bw_preload_companions **b){(void)a;(void)b;abort();}
int bw_native_launch_admission_companions_for_removal(struct bw_native_launch_admission *a,const struct bw_preload_companions **b){(void)a;(void)b;abort();}
int bw_native_launch_admission_close(struct bw_native_launch_admission **a){(void)a;abort();}
