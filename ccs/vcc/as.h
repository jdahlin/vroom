#ifndef VCC_AS_H
#define VCC_AS_H
/*
 * GNU-as-compatible assembler front end for AArch64 (the subset the kernel
 * uses), writing into an ElfObj.  One As per thread.
 */
#include <stddef.h>
#include <stdint.h>
#include "elf.h"

typedef struct As As;

As *as_new(void);
void as_free(As *a);
/* Assemble preprocessed text (no NUL needed) into o, which is reset first.
 * name is used for messages; dir resolves relative .incbin paths (NULL =
 * cwd).  Returns 0 on success; on error *msg points at a message. */
int as_run(As *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg);

/* statements run (after macro/loop expansion) and their bytes, cumulative */
void as_stats(const As *a, uint64_t *stmts, uint64_t *bytes);

extern int as_trace;

#endif
