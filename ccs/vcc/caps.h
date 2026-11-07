#ifndef VCC_CAPS_H
#define VCC_CAPS_H

/*
 * Returns how many argv entries the option consumes (1 or 2), or 0 if vcc
 * does not support it; *why is set to the offending option.
 */
int cc_option(const char *a, const char *next, const char **why);
/* 1 if vld supports the option, 0 otherwise */
int ld_option(const char *a, const char **why);

#include <stdbool.h>
#include <stddef.h>
/* true if vcc predefines the macro named in "NAME[=VALUE]" */
bool cc_predefined(const char *def);

/* `vcc --version` and `vld --version`/`-v` output (clang/lld-compatible identity) */
extern const char cc_version_text[], ld_version_text[];
/*
 * A probe's source (stdin) for the assembler or the C compiler:
 * 1 accepted, 0 rejected, -1 not in the table (compile it for real).
 */
int as_source_probe(const char *src, size_t n);
int c_source_probe(const char *src, size_t n);
/* whether vcc links user-space programs (it has no target libc) */
extern const bool cc_can_link;
/* answer to -print-file-name=include */
extern const char cc_resource_include[];
/* true if vld refuses the link option a (next: the following argument) */
bool ld_rejects(const char *a, const char *next);

#endif
