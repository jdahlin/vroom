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
#include <stdio.h>

int ld_main(int argc, char **argv, int dirfd, FILE *err);

/* arm64 Mach-O executables from MH_OBJECTs (platform/darwin/ldmacho.c);
   ld_main dispatches here when the first input is Mach-O */
int ld_macho(int argc, char **argv, int dirfd, FILE *err);

/* objcopy/nm subset used by the kernel build (objcopy.c) */
int objcopy_main(int argc, char **argv, int dirfd, FILE *err);
int nm_main(int argc, char **argv, int dirfd, FILE *out, FILE *err);

#endif
