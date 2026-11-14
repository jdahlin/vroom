/*
 * vld: our ELF linker for the arm64 kernel build (step 2.8).
 *
 * Takes an ld.lld command line and produces the same output as lld for the
 * links the kernel build does: ld -r (vmlinux.o) and the final link of
 * vmlinux driven by the kernel's linker script.  Inputs are mmap'ed, all
 * heavy phases are parallel, the output is assembled in one buffer and
 * written with a single write.
 *
 * ld_main() is reentrant across calls but runs one link at a time (it takes
 * a lock); vccd can call it directly.  Relative paths are resolved against
 * dirfd.  Diagnostics go to err.
 */
#ifndef VCC_LD_H
#define VCC_LD_H
#include <stdarg.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>

/*
 * Where a linker tool's errors and warnings go (ld/diagnostics.c): lines of
 * the form "PROGRAM: error: MESSAGE", as lld and llvm's tools write them.
 * The one diagnostics function of vld's ELF and Mach-O links, vobjcopy and
 * vnm; each run has its own.  Written under a process-wide lock, so the
 * threads of one link can report at once.
 */
struct ld_diagnostics {
	FILE *error_stream;
	const char *program;		/* "vld", "vobjcopy", "vnm" */
	atomic_int error_count;		/* errors reported, also past the limit */
	int error_limit;		/* errors printed at most; 0: no limit */
	bool limit_is_announced;	/* at the limit: "too many errors emitted, stopping now" (lld) */
	bool blank_line_after_multiline;	/* lld: a blank line after a multi-line diagnostic */
	bool last_was_multiline;
};
void ld_diagnostic_report(struct ld_diagnostics *diagnostics, const char *kind, const char *fmt, va_list ap);
void ld_diagnostic_error(struct ld_diagnostics *diagnostics, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void ld_diagnostic_warning(struct ld_diagnostics *diagnostics, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

int ld_main(int argc, char **argv, int dirfd, FILE *err);

/* arm64 Mach-O executables from MH_OBJECTs (platform/darwin/macho-linker.c);
   ld_main dispatches here when the first input is Mach-O */
int macho_link(int argc, char **argv, int dirfd, FILE *err);

/* x86-64 PE32+ executables and DLLs from COFF objects (platform/win/pe-linker.c);
   ld_main dispatches here when the first input is COFF x86-64 or -m i386pep is given */
int pe_link(int argc, char **argv, int dirfd, FILE *err);
#include <stdbool.h>
bool pe_link_wanted(int argc, char **argv, int dirfd);

/* objcopy/nm subset used by the kernel build (objcopy.c) */
int objcopy_main(int argc, char **argv, int dirfd, FILE *err);
int nm_main(int argc, char **argv, int dirfd, FILE *out, FILE *err);

#endif
