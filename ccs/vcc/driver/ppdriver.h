#ifndef VCC_PPDRIVER_H
#define VCC_PPDRIVER_H
typedef struct ppd_env {
	const char *cwd;	/* relative paths are relative to this (NULL: process cwd) */
	int outfd, errfd;	/* output without -o; diagnostics */
} ppd_env;

/* one -E job; argv[0] is ignored.  Returns the exit status. */
int ppd_run(int argc, char **argv, const ppd_env *env);
/* token-by-token comparison of ours[0..on) with the file refpath (0: same) */
int ppd_compare(const char *ours, size_t on, const char *refpath);
/* -c phase totals (ns): cc_run (pp + parse + codegen; ppd_ns_emit: codegen
 * + assembler alone), finishing and writing objects; object bytes */
extern _Atomic unsigned long long ppd_ns_cc, ppd_ns_emit, ppd_ns_obj, ppd_bytes_obj;

/* per-thread instruction/cycle totals by phase (vperf.h), summed over the
 * jobs this thread ran: cc_run (pp + parse + sema + codegen), emit_tu alone
 * (codegen + assembler, calling thread only), object layout + write */
#include "../lib/vperf.h"
enum { PPD_PH_CC, PPD_PH_EMIT, PPD_PH_OBJ, PPD_NPH };
extern _Thread_local vperf ppd_ph[PPD_NPH];
static inline void ppd_ph_add(int ph, vperf t0)
{
	vperf d = vperf_sub(vperf_now(), t0);

	ppd_ph[ph].ins += d.ins;
	ppd_ph[ph].cyc += d.cyc;
}
#endif
