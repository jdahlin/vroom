/*
 * driver-internal.h: what the files of the compile job share: the job's
 * parsed command line (struct driver_job), its text buffer, the output and
 * object state, and the functions that cross a file.
 */
#ifndef DRIVER_INTERNAL_H
#define DRIVER_INTERNAL_H
#include <errno.h>
#include <fcntl.h>
#include <setjmp.h>
#include <poll.h>
#include <sys/socket.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "common/intern.h"
#include "common/fail.h"
#include "frontend/pp/pp.h"
#include "frontend/c/cc.h"
#include "driver/driver.h"
#include "as/as.h"
#include "obj/obj.h"
#include "port/job-regions.h"
#include "common/digest.h"
#include "common/buffer.h"
#include "common/scoped-cleanup.h"
#include "arch/x64/x86features.h"

/* functions one file defines and another calls: static when the driver is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

typedef struct buf {
	char *p;
	size_t n, cap;
	vjob_region *r;	/* the first 16 MB come from this per-thread region (driver_buffer_free puts it back) */
} buf;

/* job-buffer.c */
extern vjob_region_pool driver_buffer_pool;
extern _Thread_local vjob_region *driver_buffer_region;	/* this job's (driver_job_run) */

/* One job's parsed command line and the state its pieces share: what driver_job_run_one held as
   locals before it was cut by job (Q-drv-02). */
struct driver_job {
	/* search path lists (searchpath.c): each holds at most argc entries */
	const char **dirs_q, **dirs_i, **dirs_s, **dirs_a, **all;
	int nq, ni, ns, na, n, ne;
	const char *dirs_e[64];	/* --embed-dir */
	const char *cpath, *cipath;	/* CPATH, C_INCLUDE_PATH of the job's environment */
	const char *cmp, *out, *main_file, *depfile, *deptarget;
	const char *stdin_src;	/* the input `-`, read whole */
	size_t stdin_len;
	int errfd, status, md, mdeps, mphony, dcolor, nocaret, elimit, mblimit, slimit, showid, dformat, nwargs, dstyle, dunicode;
	const char *badval;	/* an option with a value clang would reject */
	const char **wargs;
	char pmapbuf[4096];
	buf pre, cmdl, incl, imac;
	/* no --target: the host's own userland (Darwin on a Mac, glibc aarch64 or x86-64 on Linux) */
	int darwin;
	int x86;	/* x86_64-linux-gnu (PLAN-x86.md) */
	int wasm, lnx;	/* 64 or 32: wasm64/wasm32-unknown-emscripten (PLAN-wasm.md); lnx: aarch64 Linux */
	int hosted, nostdinc, asm_mode, undef, raw_asm, xlang;	/* raw_asm -1: by the suffix; xlang: -x given */
	int march_lse, mcpu_lse, lse;
	int sjlj;
	int opt, olevel, os, pie, pic, tlsmodel, sections, debug, freestanding, nobuiltin, nojt, instr, cfi, strict_align, tiny, gregs, wrapv;
	x86_features x86_features_, x86_disabled, x86_raw, x86_on, x86_off;
	const char *x86_cpu;
	int nosse, nosse2, nommx;	/* x86-64: -mno-sse, -mno-sse2, -mno-mmx (last one wins) */
	uint64_t disabled_builtins[CC_DISABLED_BUILTIN_WORDS];
	int omit_frame_pointer;	/* unspecified: x86 enables omission above -O0 */
	int sibling_calls;	/* explicit sibling-call flags are ordered independently of -O */
	int xcf, pfe_n, pfe_m, sprot, sp_gs, autozero;
	const char *sp_sym;	/* x86-64 hardening (cc_opts.xcf ...) */
	pp_opts po;
	char *pmap;
	double t0;
	int comments, nomarkers;
	int dmacros, showinc, ddefs, syntax, layout, lint, ms_anon, short_wchar, strict_flex_arrays, tokhash, emit, objmode;
	const char *stdver, *macos_min;
	int fast_root4, nocontract;
	int macos_min_flag, uchar, fmath;	/* fmath: 1 finite-only, 2 no errno, 4 __FAST_MATH__, 8 -pthread, 16 errno, 32 __ARM_FP_FAST */
	int tri, std, strict, wimpl_int, wimpl_fn, gnu89_inline, vcc_kernel, conv_warn;
	char objbuf[1024];
	/* dependency rules (depfile.c) */
	char dtbuf[4096], dfbuf[4200];
	const char *dtarget;
	int dnosys;
	const char *dfirst;
};

/* output.c: the job's object state (assembler, ELF writer, serialised output) */
extern _Thread_local as_context *driver_assembler;
extern _Thread_local ElfObj *driver_object;
extern _Thread_local uint8_t *driver_object_buffer;
extern _Thread_local vbuffer driver_wasm_object;

/* job-buffer.c */
VPRIV void bput(buf *b, const char *s, size_t n);
VPRIV void bstr(buf *b, const char *s);
VPRIV void driver_buffer_free(buf *b);
VPRIV void driver_buffer_append_define(buf *b, const char *d);
/* output.c */
VPRIV const char *at(const char *cwd, const char *path, char *buf, size_t cap);
VPRIV int write_all(int fd, const char *p, size_t n);
VPRIV bool driver_client_is_gone(void);
VPRIV void driver_output_mode_set(int fd);
VPRIV bool driver_output_temporary_name(char *t, size_t cap, const char *f);
VPRIV int write_file(const char *cwd, const char *path, const char *s, size_t n, int errfd);
VPRIV int write_out(const char *cwd, const char *out, int outfd, const char *s, size_t n, int errfd);
VPRIV unsigned long long driver_clock_nanoseconds(void);
VPRIV void obj_state(void);
VPRIV void driver_object_state_release(void);
VPRIV int driver_object_write(const char *cwd, const char *out, int outfd, int errfd, int macho);
VPRIV const char *driver_object_name(const char *main_file, char *buf, size_t cap);
/* depfile.c */
VPRIV int driver_depfile_write(const pp *p, const char *cwd, const char *depfile, const char *target, int nosys, int phony,
		     const char *first, const char *out, int outfd, int errfd);
VPRIV void driver_depfile_names(struct driver_job *job);
/* tokcmp.c */
VPRIV void driver_tokens_hash_run(struct driver_job *job, pp *p, const driver_environment *env);
/* searchpath.c */
VPRIV void driver_searchpath_alloc(struct driver_job *job, int argc, const driver_environment *env);
VPRIV void driver_searchpath_build(struct driver_job *job, const driver_environment *env);
/* predef.c */
VPRIV void driver_predef_build(struct driver_job *job);
VPRIV const char *const *driver_predef_darwin_system_directories(size_t *n);
VPRIV const char *const *driver_predef_darwin_framework_directories(size_t *n);
/* target-a64.c, target-x64.c */
VPRIV int driver_target_lse_detect(const char *v, bool cpu);
VPRIV int driver_target_features_check(struct driver_job *job, int argc, char **argv);
/* input.c */
VPRIV int driver_input_read(struct driver_job *job, const driver_environment *env);
/* explain.c */
VPRIV int explain(int argc, char **argv, const driver_environment *env);
/* cmdline.c: 0 to go on, 1 to end the job (status set), 2 to return 1 at once (the -ftls-model= error) */
VPRIV int driver_cmdline_parse(struct driver_job *job, int argc, char **argv);
VPRIV int driver_cmdline_check(struct driver_job *job);
/* warning-groups.c */
VPRIV int driver_warning_groups_check(struct driver_job *job);
/* job.c */
VPRIV const char *driver_environment_get(const driver_environment *env, const char *name);

#endif
