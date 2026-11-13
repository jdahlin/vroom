#ifndef VCC_AS_H
#define VCC_AS_H
/*
 * GNU-as-compatible assembler front end for AArch64 (the subset the kernel
 * uses), writing into an ElfObj.  One As per thread.
 */
#include <stddef.h>
#include <stdint.h>
#include "obj/obj.h"

typedef struct as_context as_context;

as_context *as_new(void);
void as_free(as_context *a);
/* Assemble preprocessed text (no NUL needed) into o, which is reset first.
 * name is used for messages; dir resolves relative .incbin paths (NULL =
 * cwd).  Returns 0 on success; on error *msg points at a message. */
int as_run(as_context *a, ElfObj *o, const char *text, size_t len, const char *name, const char **msg);

/* A persistent assembler for a TU whose text arrives in pieces (the code
 * generator: directives and inline asm, with machine code emitted straight
 * into the same ElfObj in between).  See as.c. */
void as_begin(as_context *a, ElfObj *o, const char *name);
int as_snippet_assemble(as_context *a, const char *text, size_t len);	/* 0, or -1 (as_message_get) */
int as_end(as_context *a);
const char *as_message_get(const as_context *a);
int as_is_failed(const as_context *a);
uint32_t as_fragment_current(const as_context *a);			/* current frag (section, subsection) */
uint32_t as_symbol_by_name(as_context *a, const char *name, size_t len);	/* find or create, as an operand would */
ElfVal as_symbol_value(as_context *a, uint32_t sym, int64_t c);	/* sym + c, .set variables substituted, folded */
int as_label_define_symbol(as_context *a, uint32_t sym);		/* define sym at the current position */

/* directory for relative .incbin paths (NULL: the process cwd) */
void as_include_directory_set(as_context *a, const char *dir);
/* Apple's assembler dialect (as vcc -S and clang write it for Mach-O) as the
 * GNU/ELF one as_run parses, line for line: the inverse of the emitter's
 * darwin_text (sections, x@PAGEOFF, Lname, .private_extern, ';' comments).
 * Returns malloc'ed text (padded: 64 zero bytes after *outn). */
char *macho_dialect_translate(const char *t, size_t n, size_t *outn);

/* statements run (after macro/loop expansion) and their bytes, cumulative */
void as_statistics_get(const as_context *a, uint64_t *stmts, uint64_t *bytes);

extern int as_trace_level;

#endif
