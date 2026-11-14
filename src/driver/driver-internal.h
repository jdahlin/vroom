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
#include "frontend/cxx/cxx.h"
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

typedef struct driver_buffer {
	char *data;
	size_t length, capacity;
	vjob_region *region;	/* the first 16 MB come from this per-thread region (driver_buffer_free puts it back) */
} driver_buffer;

/* job-buffer.c */
extern vjob_region_pool driver_buffer_pool;
extern _Thread_local vjob_region *driver_buffer_region;	/* this job's (driver_job_run) */

/* One job's parsed command line and the state its pieces share: what driver_job_run_one held as
   locals before it was cut by job (Q-drv-02). */
struct driver_job {
	/* search path lists (searchpath.c): each holds at most argc entries */
	const char **quote_directories, **include_directories, **system_directories, **after_directories, **search_directories;
	int quote_count, include_count, system_count, after_count, search_count, embed_count;
	const char *embed_directories[64];	/* --embed-dir */
	const char *cpath_value, *c_include_path_value;	/* CPATH, C_INCLUDE_PATH of the job's environment */
	const char *compare_file, *output_file, *main_file, *depfile, *dependency_target;
	const char *stdin_src;	/* the input `-`, read whole */
	size_t stdin_len;
	int errfd, status, dependency_file_mode, dependency_print_mode, dependency_phony, diagnostic_color, no_caret, error_limit, macro_backtrace_limit, spell_checking_limit, show_diagnostic_id, diagnostic_format, warning_argument_count, diagnostic_style, diagnostic_unicode;
	const char *bad_value_option;	/* an option with a value clang would reject */
	const char **warning_arguments;
	char prefix_map_buffer[4096];
	driver_buffer predefines, command_line_defines, include_text, include_macros_text;
	/* no --target: the host's own userland (Darwin on a Mac, glibc aarch64 or x86-64 on Linux) */
	int darwin;
	int target_x64;	/* x86_64-linux-gnu (PLAN-x86.md) */
	int wasm_bits, target_linux;	/* 64 or 32: wasm64/wasm32-unknown-emscripten (PLAN-wasm.md); lnx: aarch64 Linux */
	int hosted, no_standard_includes, asm_mode, undefine_predefined, raw_assembler, language_given;	/* raw_asm -1: by the suffix; xlang: -x given */
	int march_has_lse, mcpu_has_lse, lse;
	int sjlj;
	int optimize, optimization_level, optimize_size, pie, pic, tls_model, function_data_sections, debug, freestanding, no_builtin, no_jump_tables, instrument_functions, cfi, strict_align, code_model_tiny, general_regs_only, wrapv;
	x86_features x64_features, x64_features_disabled, x64_features_raw, x64_features_forced_on, x64_features_forced_off;
	const char *x64_cpu;
	int no_sse, no_sse2, no_mmx;	/* x86-64: -mno-sse, -mno-sse2, -mno-mmx (last one wins) */
	uint64_t disabled_builtins[CC_DISABLED_BUILTIN_WORDS];
	int omit_frame_pointer;	/* unspecified: x86 enables omission above -O0 */
	int sibling_calls;	/* explicit sibling-call flags are ordered independently of -O */
	int x64_hardening_flags, patchable_entry_count, patchable_entry_before, stack_protector, stack_protector_guard_gs, auto_variables_zero;
	const char *stack_protector_guard_symbol;	/* x86-64 hardening (cc_opts.xcf ...) */
	pp_opts pp_options;
	char *prefix_map;
	double start_time;
	int comments, no_line_markers;
	int dump_macros, show_includes, dump_defines, syntax_only, layout_checks, lint, ms_anonymous_structs, short_wchar, strict_flex_arrays, token_hash, emit_assembly, object_mode;
	const char *standard_version, *macos_minimum_version;
	int fast_math, no_fp_contract;
	int macos_minimum_source, unsigned_char, math_flags;	/* fmath: 1 finite-only, 2 no errno, 4 __FAST_MATH__, 8 -pthread, 16 errno, 32 __ARM_FP_FAST */
	/* C++ (PLAN-cxx.md X2): cxx is the standard's year (98 .. 26), 0 for C; cxx_given: -std=c++NN seen */
	int cxx, cxx_given, cxx_libcxx, no_exceptions, no_rtti, no_standard_cxx_includes;
	int trigraphs, standard, strict_standard, allow_implicit_int, allow_implicit_function, gnu89_inline, vcc_kernel, conversion_warning_flags;
	char object_name_buffer[1024];
	/* dependency rules (depfile.c) */
	char dependency_target_buffer[4096], depfile_name_buffer[4200];
	const char *depfile_target;
	int depfile_omit_system;
	const char *depfile_first_input;
};

/* output.c: the job's object state (assembler, ELF writer, serialised output) */
extern _Thread_local as_context *driver_assembler;
extern _Thread_local ElfObj *driver_object;
extern _Thread_local uint8_t *driver_object_buffer;
extern _Thread_local vbuffer driver_wasm_object;

/* job-buffer.c */
VPRIV void driver_buffer_append(driver_buffer *b, const char *s, size_t n);
VPRIV void driver_buffer_append_string(driver_buffer *b, const char *s);
VPRIV void driver_buffer_free(driver_buffer *b);
VPRIV void driver_buffer_append_define(driver_buffer *b, const char *d);
/* output.c */
VPRIV const char *driver_path_resolve(const char *cwd, const char *path, char *buf, size_t cap);
VPRIV int driver_output_write_all(int fd, const char *p, size_t n);
VPRIV bool driver_client_is_gone(void);
VPRIV void driver_output_mode_set(int fd);
VPRIV bool driver_output_temporary_name(char *t, size_t cap, const char *f);
VPRIV int driver_output_file_write(const char *cwd, const char *path, const char *s, size_t n, int errfd);
VPRIV int driver_output_write(const char *cwd, const char *out, int outfd, const char *s, size_t n, int errfd);
VPRIV unsigned long long driver_clock_nanoseconds(void);
VPRIV void driver_object_state_acquire(void);
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
VPRIV int driver_explain_run(int argc, char **argv, const driver_environment *env);
/* cmdline.c: 0 to go on, 1 to end the job (status set), 2 to return 1 at once (the -ftls-model= error) */
VPRIV int driver_cmdline_parse(struct driver_job *job, int argc, char **argv);
VPRIV int driver_cmdline_check(struct driver_job *job);
/* warning-groups.c */
VPRIV int driver_warning_groups_check(struct driver_job *job);
/* job.c */
VPRIV const char *driver_environment_get(const driver_environment *env, const char *name);

#endif
