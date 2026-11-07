#ifndef VCC_PP_H
#define VCC_PP_H
/*
 * vcc preprocessor: C (gnu11) translation phase 4, pull model.
 *
 * pp_next() hands out one fully macro-expanded token at a time.  Files come
 * from the token cache (lex.h) and are never copied: a file context and an
 * object-like macro body are (pointer, index) into the immutable token
 * arrays.  Only function-like expansions write tokens, into a per-TU arena
 * that is reset whenever the context stack is back at file level.
 *
 * Expansion follows clang's model exactly (it is the parity target): a
 * "disabled" bit per macro while its expansion is on the stack, a paint bit
 * (PF_NOEXP) on identifier tokens read while their macro was disabled, and
 * clang's argument pre-expansion, paste and whitespace rules.
 */
#include <setjmp.h>
#include <stdarg.h>

#include "lex.h"

enum {
	P_DIRECTIVE = T_NKINDS,	/* a line to pass through: s = "pragma ...", "ident ..." */
};

enum {
	PF_NOEXP = 16,		/* painted: never expand (C11 6.10.3.4p2) */
	PF_NOPASTE = 32,	/* a ## that came from an argument */
};

typedef struct ptok {
	const char *s;		/* spelling; raw (with splices) if TF_DIRTY */
	uint32_t len;
	uint32_t id;		/* interned id (T_IDENT) */
	uint32_t loc;		/* TU source location (file base + offset) */
	uint8_t kind, flags;	/* vtok_kind / P_*, TF_* | PF_* */
	uint16_t _pad;
} ptok;

typedef struct pp_opts {
	const char *main;		/* the source file */
	const char *const *dirs;	/* search path: -iquote dirs, then -I, -isystem, -idirafter */
	int ndirs, nquote;		/* dirs[0..nquote) only for "" includes */
	int nsys;			/* dirs[nsys..) are system directories (0: none) */
	const char *predef;		/* "<built-in>": #define/#undef/#include lines, run first */
	const char *prefix_from, *prefix_to;	/* -fmacro-prefix-map (one) */
	const char *const *fwdirs;	/* framework dirs: <A/b.h> -> DIR/A.framework/Headers/b.h */
	int nfwdirs;
	int unsigned_char;
	const char *cwd;		/* relative paths are relative to this (NULL: process cwd) */
	int errfd;			/* diagnostics (0: stderr) */
	int asm_mode;			/* assembler-with-cpp: unknown directives pass, # need not stringify */
	int target;			/* PP_LINUX_AARCH64 or PP_DARWIN (column of the feature table) */
} pp_opts;

enum { PP_LINUX_AARCH64, PP_DARWIN };

typedef struct pp pp;

/* NULL if the main file cannot be read (reported) */
pp *pp_open(const pp_opts *o);
void pp_close(pp *p);
/* fatal errors (missing #include) longjmp here instead of exiting */
void pp_set_fatal(pp *p, jmp_buf *jb);
/* the distinct files read so far, as spelled (for -MD dependency files) */
size_t pp_files(const pp *p, const char **names, size_t max);
/* the next token; T_EOF at the end (and ever after) */
void pp_next(pp *p, ptok *t);
/* errors reported so far */
int pp_errors(const pp *p);

/* is loc in a system header */
int pp_in_system_header(const pp *p, uint32_t loc);
/* presumed file name, line and column of a location; 0 if it has none */
int pp_where(const pp *p, uint32_t loc, const char **name, uint32_t *namelen, uint32_t *line, uint32_t *col);
/* a diagnostic at loc ("error", "warning", "fatal error": longjmp/exit) */
void pp_vdiag(pp *p, uint32_t loc, const char *what, const char *fmt, va_list ap);

/* the token's spelling without splices; buf must hold t->len bytes */
const char *pp_spell(const ptok *t, char *buf, size_t *len);

#endif
