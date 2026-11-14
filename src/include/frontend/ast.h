#ifndef VCC_FRONTEND_AST_H
#define VCC_FRONTEND_AST_H
/*
 * The AST that code generation reads, shared by the C front end
 * (frontend/c/) and the C++ front end (frontend/cxx/, PLAN-cxx.md X1).
 *
 * The record types (ctype, field, tag, sym, node and their enumerators) are
 * in frontend/c/cc.h; here are the side tables and struct ast, the tables
 * and the few TU-wide values codegen/ and arch/<cpu>/codegen/ read.  A
 * front end owns a struct ast and fills it; the C front end embeds it as the
 * first member of struct cc (ccint.h), which the layout measurement
 * (doc/quality/MEASURE-ast-split.md) requires: the per-node tables first, o
 * next to ty, under 504 bytes in all.
 */
#include <stddef.h>
#include "frontend/c/cc.h"

typedef __int128 i128;
typedef unsigned __int128 u128;

typedef struct cstr {		/* a string literal */
	const char *s;		/* bytes, element size each, zero terminated */
	uint32_t n;		/* elements, terminator included */
	uint8_t esz;		/* element size: 1, 2, 4 */
	uint8_t kind;		/* LEX_TOKEN_STRING / LEX_TOKEN_WIDE_STRING / ... */
} cstr;

typedef struct label {
	uint32_t name, loc;
	uint32_t defined;	/* node of the N_LABEL, 0 if only referenced */
	uint32_t shadow;	/* __label__ scoping */
	uint32_t depth;
	uint8_t local;		/* declared with __label__ */
	uint8_t addr_taken;
} label;

/* Call-site restrictions kept with declaration attributes, including redeclarations. */
enum {
	AF_TAIL_RETURNS_TWICE = 1u << 0,
	AF_TAIL_DISABLE = 1u << 1,
	AF_TAIL_NOT_CALLED = 1u << 2,
	AF_OPTNONE = 1u << 3,	/* currently prevents the new sibling-call transformation */
};

typedef struct attrs {		/* the attributes of one declaration */
	x86_features x86_features;
	uint32_t x86_target;	/* effective function target features are present */
	uint32_t aligned;	/* bytes; 0: none */
	uint32_t section, alias, asmname;	/* string index + 1, 0: none */
	uint32_t cleanup;	/* sym */
	uint32_t mode;		/* 0, or the integer type kind from mode() */
	uint32_t vecsize;	/* vector_size(n) bytes, 0: none */
	uint32_t extvec;	/* ext_vector_type(n) elements, 0: none */
	uint32_t depr;		/* deprecated: the attribute name's location, 0: none */
	uint32_t deprmsg;	/* deprecated("msg"): string index + 1, 0: none */
	uint32_t wur;		/* 1: warn_unused_result, 2: [[nodiscard]] */
	uint32_t nonnull;	/* nonnull: bit 0 all pointer parameters, bit i parameter i (1-based) */
	uint32_t fmt;		/* format(printf, i, j): 1 << 16 | i << 8 | j, 0: none or not printf */
	uint32_t ifunc;		/* ifunc("resolver"): alias is the resolver, an STT_GNU_IFUNC symbol */
	uint32_t counted_by;	/* identifier id */
	uint32_t errmsg;	/* error("..."): string index + 1, 0: none */
	uint32_t interrupt;	/* x86 interrupt calling convention */
	uint32_t ms_struct;	/* Microsoft record layout */
	uint32_t tlsloc;		/* tls_model attribute location */
	uint32_t visibility;
	uint32_t flags;		/* AF_* declaration/call restrictions */
	uint8_t packed, transparent, used, unused, weak, noreturn, constructor, destructor;
	uint8_t nonstring, may_alias, cold, gnu_inline, always_inline, noinline, ainline, error_fn;
	uint8_t overloadable, noinstr;
	uint8_t pure;		/* 1: pure, 2: const (-Wunused-value) */
	uint8_t unavail;	/* unavailable("msg"): depr/deprmsg say where and why, uses are errors */
	uint8_t fmtarg;		/* format_arg(i) */
	uint16_t prio;		/* constructor(N)/destructor(N), 0: none (65535) */
	uint8_t tlsmodel;	/* tls_model(): VCC_TLS_*, 0: none */
} attrs;

typedef struct asmop {
	uint32_t name;		/* [name] id, 0 */
	uint32_t constraint;	/* string index */
	uint32_t expr;		/* node */
} asmop;

typedef struct casm {
	uint32_t tmpl;		/* string index */
	uint32_t nout, nin, nclob, nlabel;
	asmop *ops;		/* outputs then inputs */
	uint32_t *clob;		/* string indices */
	uint32_t *labels;	/* label indices */
	uint8_t vol, go, inl, extended;
} casm;

/*
 * The fields, in this order (the measurement's): the per-node tables first.
 * A macro so that struct cc can hold them both as a named member (c->ast)
 * and as an anonymous one (c->syms) with one layout.
 */
#define AST_FIELDS \
	pp *p;			/* diagnostics */ \
	const cc_opts *o; \
	ctype *ty; \
	uint32_t nty; \
	qty *params; \
	uint32_t nparams; \
	tag *tags; \
	uint32_t ntag; \
	sym *syms; \
	uint32_t nsym; \
	node *nodes; \
	uint32_t nnode; \
	uint32_t *lists; \
	uint32_t nlist; \
	i128 *ipool; \
	uint32_t nipool; \
	double *fpool;		/* a 16-byte long double: its double, then its binary128 bits in the next two */ \
	uint32_t nfpool; \
	cstr *strs; \
	uint32_t nstr; \
	label *labels; \
	uint32_t nlabel; \
	attrs *attr; \
	uint32_t nattr; \
	casm *asms; \
	uint32_t nasm; \
	qty t_size, t_ptrdiff, t_wchar, t_char16, t_char32, t_valist;	/* the TU's builtin types */ \
	uint32_t ntoplevel; \
	uint32_t *toplevel;	/* file-scope syms in declaration order */ \
	uint32_t *tlasm;	/* file-scope asm("...") statements (N_ASM nodes) */ \
	uint32_t ntlasm;

struct ast { AST_FIELDS };
#endif
