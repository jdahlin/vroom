#ifndef VCC_AS_H
#define VCC_AS_H
/*
 * GNU-as-compatible assembler front end for AArch64 (the subset the kernel
 * uses), writing into an ElfObj.  One As per thread.
 */
#include <stddef.h>
#include <stdint.h>
#include "../obj/obj.h"

typedef struct As As;

As *as_new(void);
void as_free(As *a);
/* Assemble preprocessed text (no NUL needed) into o, which is reset first.
 * name is used for messages; dir resolves relative .incbin paths (NULL =
 * cwd).  Returns 0 on success; on error *msg points at a message. */
int as_run(As *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg);

/* A persistent assembler for a TU whose text arrives in pieces (the code
 * generator: directives and inline asm, with machine code emitted straight
 * into the same ElfObj in between).  See as.c. */
void as_begin(As *a, ElfObj *o, const char *name);
int as_snippet(As *a, const char *text, size_t len);	/* 0, or -1 (as_msg) */
int as_end(As *a);
const char *as_msg(const As *a);
int as_failed(const As *a);
uint32_t as_frag(const As *a);			/* current frag (section, subsection) */
uint32_t as_sym(As *a, const char *name, size_t len);	/* find or create, as an operand would */
ElfVal as_val(As *a, uint32_t sym, int64_t c);	/* sym + c, .set variables substituted, folded */
int as_label(As *a, uint32_t sym);		/* define sym at the current position */

/* directory for relative .incbin paths (NULL: the process cwd) */
void as_set_dir(As *a, const char *dir);
/* Apple's assembler dialect (as vcc -S and clang write it for Mach-O) as the
 * GNU/ELF one as_run parses, line for line: the inverse of the emitter's
 * darwin_text (sections, x@PAGEOFF, Lname, .private_extern, ';' comments).
 * Returns malloc'ed text (padded: 64 zero bytes after *outn). */
char *as_darwin_text(const char *t, size_t n, size_t *outn);

/* statements run (after macro/loop expansion) and their bytes, cumulative */
void as_stats(const As *a, uint64_t *stmts, uint64_t *bytes);

extern int as_trace;

#endif
