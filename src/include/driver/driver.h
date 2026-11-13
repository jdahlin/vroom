/*
 * driver.h: the compile job's interface: run one job from a clang command line (driver_job_run), compare
 * a token stream against a reference (driver_tokens_compare), and the counters of a -c job's phases.
 *
 * The module (src/driver/, job.c is its entry) turns a clang command line into one job: -E, -S/-c
 * (C or assembler), --cmp, --vcc-tokhash or --explain, and writes the output; it is the only thing
 * vpp and vccd call to compile.  driver_job_run is thread safe and touches no process state: the
 * working directory, the environment and the file descriptors come in through driver_environment, so under vccd
 * they are the client's.  It returns the exit status (0 ok, 1 failed, 2 usage) and reports on
 * env->errfd.  The thread-local driver_client_umask and driver_client_connection are set by the caller (vccd's dispatcher and
 * worker) before the call.  The phase totals are atomics summed over all jobs of the process; the
 * vperf per-thread ones are for the thread that ran the job.  Depends on the preprocessor and the
 * front end; the modules above it (server/, bin/) include only this header.
 */
#ifndef VCC_PPDRIVER_H
#define VCC_PPDRIVER_H
typedef struct driver_environment {
	const char *cwd;	/* relative paths are relative to this (NULL: process cwd) */
	int outfd, errfd;	/* output without -o; diagnostics */
	char **envp;		/* the client's environment (vccd); NULL: this process's */
	struct pp_dsink *dsink;	/* vccd: the build's diagnostics store (NULL: none) */
	int errtty;		/* errfd is a capture of a terminal's diagnostics (vccd): colors as for it */
	int infd;		/* the input `-` is read from (0: this process's stdin) */
	const char *in_src;	/* non-NULL: the input `-` already read (vccd keeps it for a fallback) */
	size_t in_len;
} driver_environment;

/* the client's umask for files a job creates (-1: the process's own) */
extern _Thread_local int driver_client_umask;
/* vccd: the client's connection; a job whose client has gone writes no output */
extern _Thread_local int driver_client_connection;

/* one -E job; argv[0] is ignored.  Returns the exit status. */
int driver_job_run(int argc, char **argv, const driver_environment *env);
/* token-by-token comparison of ours[0..on) with the file refpath (0: same) */
int driver_tokens_compare(const char *ours, size_t on, const char *refpath);
/* -c phase totals (ns): cc_run (pp + parse + codegen; driver_emit_nanoseconds: codegen
 * + assembler alone), finishing and writing objects; object bytes */
extern _Atomic unsigned long long driver_compile_nanoseconds, driver_emit_nanoseconds, driver_object_nanoseconds, driver_object_bytes;
/* -c totals: tokens the parser took from pp_token_next, tokens skipped (decl cache) */
extern _Atomic unsigned long long driver_tokens_taken, driver_tokens_skipped;

/* per-thread instruction/cycle totals by phase (vperf.h), summed over the
 * jobs this thread ran: cc_run (pp + parse + sema + codegen), emit_tu alone
 * (codegen + assembler, calling thread only), object layout + write */
#include "port/performance-counters.h"
enum { DRIVER_PHASE_COMPILE, DRIVER_PHASE_EMIT, DRIVER_PHASE_OBJECT, DRIVER_PHASE_COUNT };
extern _Thread_local vperf driver_phase_counters[DRIVER_PHASE_COUNT];
static inline void driver_phase_add(int ph, vperf t0)
{
	vperf d = vperformance_subtract(vperformance_now(), t0);

	driver_phase_counters[ph].ins += d.ins;
	driver_phase_counters[ph].cyc += d.cyc;
}
#endif
