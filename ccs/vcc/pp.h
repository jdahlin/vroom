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
	const char *predef;		/* "<built-in>": #define/#undef/#include lines, run first */
	const char *prefix_from, *prefix_to;	/* -fmacro-prefix-map (one) */
	const char *const *fwdirs;	/* framework dirs: <A/b.h> -> DIR/A.framework/Headers/b.h */
	int nfwdirs;
	int unsigned_char;
	int target;			/* PP_LINUX_AARCH64 or PP_DARWIN (column of the feature table) */
} pp_opts;

enum { PP_LINUX_AARCH64, PP_DARWIN };

typedef struct pp pp;

pp *pp_open(const pp_opts *o);
/* the next token; T_EOF at the end (and ever after) */
void pp_next(pp *p, ptok *t);
/* errors reported so far */
int pp_errors(const pp *p);

/* the token's spelling without splices; buf must hold t->len bytes */
const char *pp_spell(const ptok *t, char *buf, size_t *len);

#endif
