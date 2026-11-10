#ifndef VCC_CCINT_H
#define VCC_CCINT_H
/*
 * vcc front end internals shared by cc/ (parser + sema) and emit/ (code
 * generator): the per-TU state and the AST side tables.
 */
#include <setjmp.h>
#include "cc.h"
#include "../lib/intern.h"

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

typedef __int128 i128;
typedef unsigned __int128 u128;

/* ---------------- per-TU state ---------------- */

#define DIR_BITS 11
#define DIR_PAGE (1u << DIR_BITS)
#define NDIR ((1u << VINTERN_ID_BITS) >> DIR_BITS)

typedef struct cstr {		/* a string literal */
	const char *s;		/* bytes, element size each, zero terminated */
	uint32_t n;		/* elements, terminator included */
	uint8_t esz;		/* element size: 1, 2, 4 */
	uint8_t kind;		/* T_STRING / T_WSTRING / ... */
} cstr;

typedef struct label {
	uint32_t name, loc;
	uint32_t defined;	/* node of the N_LABEL, 0 if only referenced */
	uint32_t shadow;	/* __label__ scoping */
	uint32_t depth;
	uint8_t local;		/* declared with __label__ */
	uint8_t addr_taken;
} label;

typedef struct attrs {		/* the attributes of one declaration */
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
	uint32_t visibility;
	uint32_t flags;		/* SF_* */
	uint8_t packed, transparent, used, unused, weak, noreturn, constructor, destructor;
	uint8_t nonstring, may_alias, cold, gnu_inline, always_inline, noinline, ainline, error_fn;
	uint8_t overloadable, noinstr;
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
	uint8_t vol, go, inl;
} casm;

/*
 * Lazy function bodies: a static function defined in a header keeps its
 * body as tokens (lztok, spellings of non-identifiers in lzs) and is parsed
 * after the TU only if something names it (SF_REF), like clang's deferred
 * emission one step earlier.  Most of a kernel TU's tokens are such bodies.
 */
typedef struct lztok {
	uint32_t loc;
	uint32_t v;		/* T_IDENT: interned id; otherwise: offset of the spelling in lzs */
	uint32_t len;
	uint8_t kind, flags;
	uint16_t _pad;
} lztok;

typedef struct lzfn {
	uint32_t sym;
	uint32_t tk, ntk;	/* its tokens in lzt, '{' to '}' */
	uint32_t wm;		/* nsym at the definition: later file-scope syms are not visible */
	uint32_t name, loc, psyms, npsyms, plb, ple;	/* the declarator, for function_def */
	uint8_t done;
	uint8_t hc;		/* the tokens are z's, in a header cache entry (ntk 0) */
	pp_lazy z;
} lzfn;

struct cc {
	pp *p;
	const cc_opts *o;
	jmp_buf bail;
	int errors;

	/* tokens: tk[0] is the current one */
	ptok tk[4];
	int ntk;
	uint32_t prevloc;
	uint8_t prevflags;
	uint32_t dead;		/* inside if (0) ... (and the like): no runtime-behaviour warnings */	/* the previous token's flags (PF_MACRO: diagnostics) */
	uint8_t defer_rt;	/* parsing a static initializer: -Wdivision-by-zero waits (clang's DiagRuntimeBehavior) */
	uint8_t ndefer;
	struct { uint32_t loc; pp_span r; uint8_t rem; } defer[8];
	uint32_t unk_loc;	/* the last unknown token given -Winvalid-pp-token */
	uint8_t se_direct;	/* the next statement is one of a statement expression's body */
	uint16_t nrtd;		/* warnings of a function body given at its end (clang's DiagRuntimeBehavior) */
	struct { const char *flag; const char *const *groups; const char *ms; uint32_t mlen, loc, nr; pp_span r[2];
		 char msg[112]; } rtd[64];
	bool after_amp;		/* the unary * being parsed is the operand of & (no -Wvoid-ptr-dereference) */
	uint8_t cv_what;		/* context of the conversion being checked: CX_* (diagnostics) */
	uint32_t cv_loc, cv_fn, cv_arg, cv_callee;	/* cv_callee: CX_PASS: the callee expression's first location */	/* CX_PASS: the callee's sym (0: unknown) and the argument */

	ctype *ty;
	uint32_t nty;
	uint32_t *tyhash;	/* interning, open addressing */
	uint32_t tyhcap;
	qty *params;
	uint32_t nparams;
	uint32_t *fnhash, fnhcap, nfn;
	uint32_t *ptrc;		/* qty -> type index of a pointer to it (a cache, checked on use) */
	uint32_t ptr_lo;	/* ptrc entries below are not used (1; a recorded region: its first type) */

	tag *tags;
	uint32_t ntag;
	uint32_t cplx[TY_NKINDS];	/* _Complex <kind>: its record type (0: not made yet) */
	sym *syms;
	uint32_t nsym;
	node *nodes;
	pp_span *span;		/* per node: first and last token (start locations), 0: unknown */
	uint32_t nnode;
	uint32_t *lists;
	uint32_t nlist;
	i128 *ipool;
	uint32_t nipool;
	double *fpool;		/* a 16-byte long double: its double, then its binary128 bits in the next two */
	uint32_t nfpool;
	cstr *strs;
	uint32_t nstr;
	label *labels;
	uint32_t nlabel;
	attrs *attr;
	uint32_t nattr;
	casm *asms;
	uint32_t nasm;

	char *heap, *hp, *hend;	/* bump allocator for everything else */
	struct dop *dops;	/* declarator op stack (cc/declarator.c) */
	uint32_t ndops;

	/* scopes: identifier id -> innermost binding (sym index) */
	uint32_t *odir[NDIR], *tdir[NDIR], *ldir[NDIR];
	uint32_t *sstack;	/* syms bound in open scopes */
	uint32_t nsstack;
	uint32_t smark[4096];
	uint32_t depth;

	/* the function being parsed */
	uint32_t fn;		/* sym */
	uint32_t fn_labels;	/* first label of this function */
	uint32_t *switch_cases;	/* N_CASE/N_DEFAULT nodes of the innermost switch */
	int in_loop, in_switch, in_stmtexpr;
	bool sw_default;	/* the innermost switch has a default label */
	struct swcase { i128 lo, hi; uint32_t loc; } *cases;	/* the open switches' case labels */
	uint32_t ncase, capcase, case_base;
	qty switch_ty;
	int unevaluated;	/* sizeof / typeof / _Alignof operand */
	attrs pend;		/* attributes before a block-scope declaration */
	int in_struct;		/* parsing a struct/union body */
	bool has_pend;
	uint32_t pack[64];	/* #pragma pack stack (bytes, 0: none) */
	uint64_t packid[64];	/* labels (token.c pragma_pack) */
	int npack;
	/* #pragma weak (decl.c): waiting for key's declaration (alias: 0, or the weak alias of key) */
	struct pweak { uint32_t key, alias, loc, sym; } *pweak;
	uint32_t npweak, cpweak;	/* (the done aliases, sym set, stay for the end-of-TU check) */

	/* builtin types */
	qty t_size, t_ptrdiff, t_wchar, t_char16, t_char32, t_valist;
	uint32_t valist_tag;
	uint32_t id_func, id_FUNCTION, id_PRETTY, id_main;
	uint32_t id_int128_t, id_uint128_t, id_valist;
	uint32_t id_null[4];	/* _Nonnull _Nullable _Null_unspecified _Nullable_result */
	uint32_t func_name_sym;
	uint32_t ntoplevel;
	uint32_t *toplevel;	/* file-scope syms in declaration order */
	uint32_t *tlasm;	/* file-scope asm("...") statements (N_ASM nodes) */
	uint32_t ntlasm;
	uint32_t ndiag;		/* diagnostics reported */
	struct dcst *dc;	/* declaration cache (ccsnap.c), NULL: off */

	/* lazy bodies */
	lztok *lzt;
	uint32_t nlzt;
	lzfn *lzf;
	uint32_t nlzf;
	char *lzs;
	uint32_t nlzs;
	bool lz_on;		/* tokens come from lzt[lzc, lze) or lzz (lz_hc), then T_EOF */
	bool lz_hc;
	uint32_t lzc, lze;
	pp_lazy lzz;
	uint32_t lz_wm;		/* parsing a lazy body: file-scope syms from here on are hidden (0: none) */

	/* VCC_DRCOV: delivered positions of file-scope declaration starts (UINT64_MAX: lookahead) */
	uint64_t *drb;
	uint32_t ndrb, cdrb;
	/* VCC_DR / VCC_DRREC: per-region declaration replay (cc/replay.c) */
	struct drrec *dr;
	uint8_t dr_on;		/* a region is being recorded: the hooks log */
};


enum {
	BI_NONE,
	/* special forms (own parsing) */
	BI_CONSTANT_P, BI_EXPECT, BI_EXPECT_PROB, BI_CHOOSE, BI_TYPES_COMPAT, BI_OFFSETOF,
	BI_VA_START, BI_VA_END, BI_VA_COPY, BI_VA_ARG,
	BI_CLZ, BI_CLZL, BI_CLZLL, BI_CTZ, BI_CTZL, BI_CTZLL, BI_FFS, BI_FFSL, BI_FFSLL,
	BI_POPCOUNT, BI_POPCOUNTL, BI_POPCOUNTLL, BI_PARITY, BI_PARITYL, BI_PARITYLL,
	BI_CLRSB, BI_CLRSBL, BI_CLRSBLL, BI_CLZG, BI_CTZG, BI_POPCOUNTG, BI_SHUFFLEVECTOR, BI_CONVERTVECTOR,
	BI_BSWAP16, BI_BSWAP32, BI_BSWAP64, BI_BITREVERSE8, BI_BITREVERSE16, BI_BITREVERSE32, BI_BITREVERSE64,
	BI_UNREACHABLE, BI_TRAP, BI_DEBUGTRAP, BI_RETURN_ADDRESS, BI_FRAME_ADDRESS, BI_EXTRACT_RETURN_ADDR,
	BI_ADD_OVERFLOW, BI_SUB_OVERFLOW, BI_MUL_OVERFLOW, BI_ADDC, BI_SUBC,
	BI_OBJECT_SIZE, BI_DYNAMIC_OBJECT_SIZE, BI_COUNTED_BY_REF,
	BI_ALLOCA, BI_ALLOCA_UNINIT, BI_PREFETCH, BI_ASSUME_ALIGNED, BI_ASSUME,
	BI_CLASSIFY_TYPE, BI_IS_CONSTANT_EVALUATED, BI_STRLEN,
	BI_HUGE_VAL, BI_HUGE_VALF, BI_INF, BI_INFF, BI_NAN, BI_NANF,
	BI_ISNAN, BI_ISINF, BI_ISFINITE, BI_SIGNBIT, BI_FPCLASSIFY, BI_ISINF_SIGN, BI_ISNORMAL,
	BI_ISGREATER, BI_ISGREATEREQUAL, BI_ISLESS, BI_ISLESSEQUAL, BI_ISLESSGREATER, BI_ISUNORDERED,
	BI_CPU_SUPPORTS, BI_ARM_RSR,
	/* GNU __atomic_* (memory orders are evaluated, the code is always seq_cst) */
	BI_ATOMIC_LOAD_N, BI_ATOMIC_STORE_N, BI_ATOMIC_EXCHANGE_N, BI_ATOMIC_CMPXCHG_N,
	BI_ATOMIC_LOAD, BI_ATOMIC_STORE, BI_ATOMIC_EXCHANGE, BI_ATOMIC_CMPXCHG,
	BI_ATOMIC_FETCH_ADD, BI_ATOMIC_FETCH_SUB, BI_ATOMIC_FETCH_AND, BI_ATOMIC_FETCH_OR, BI_ATOMIC_FETCH_XOR,
	BI_ATOMIC_ADD_FETCH, BI_ATOMIC_SUB_FETCH, BI_ATOMIC_AND_FETCH, BI_ATOMIC_OR_FETCH, BI_ATOMIC_XOR_FETCH,
	BI_ATOMIC_THREAD_FENCE, BI_ATOMIC_SIGNAL_FENCE, BI_ATOMIC_LOCK_FREE,
	/* legacy __sync_* without an __atomic_* form (the others map onto __atomic_*) */
	BI_SYNC_VAL_CAS, BI_SYNC_BOOL_CAS, BI_SYNC_LOCK_RELEASE, BI_SYNC_SYNCHRONIZE,
	BI_CONJ, BI_CREAL, BI_CIMAG,	/* complex parts (the element type is the signature's return) */
	BI_ROTL8, BI_ROTL16, BI_ROTL32, BI_ROTL64, BI_ROTR8, BI_ROTR16, BI_ROTR32, BI_ROTR64,
	/* ACLE hints and barriers (side effects: BI_ARM_NOP..BI_ARM_TRAP) */
	BI_ARM_NOP, BI_ARM_YIELD, BI_ARM_WFE, BI_ARM_WFI, BI_ARM_SEV, BI_ARM_SEVL, BI_ARM_CLREX,
	BI_ARM_ISB, BI_ARM_DMB, BI_ARM_DSB, BI_ARM_TRAP,
	BI_LIB,	/* plain library call with a prototype */
	BI_COUNT
};

enum { CV_NONE, CV_INT, CV_FLT, CV_ADDR };

typedef struct cval {
	int kind;
	bool ice;		/* integer constant expression (C11 6.6p6, clang's rules) */
	i128 i;			/* CV_INT; CV_ADDR: byte offset */
	double f;		/* CV_FLT; with ld, the nearest double of q */
	u128 q;			/* CV_FLT with ld: the binary128 value (16-byte long double) */
	bool ld;
	uint32_t sym, str;	/* CV_ADDR base: sym or string literal */
	uint32_t label;		/* CV_ADDR base: &&label */
	uint32_t sublabel;	/* CV_ADDR: minus &&sublabel (GNU label difference: a constant once laid out) */
} cval;

#define CT(c, t) (&(c)->ty[QI(t)])
#define CKIND(c, t) ((c)->ty[QI(t)].kind)
#define CN(c, i) (&(c)->nodes[i])

bool cc_eval(cc *c, uint32_t n, cval *v);
void dr_restored(cc *c);
bool cc_is_signed(cc *c, qty t);
const char *cc_builtin_name(int64_t i);
const char *cc_lib_sig(const char *name, int *clang_enc);
uint32_t cc_global_named(cc *c, const char *s);

#endif
