#ifndef VCC_CCINT_H
#define VCC_CCINT_H
/*
 * vcc front end internals shared by frontend/c/ (parser + sema) and codegen/ (code
 * generator): the per-TU state and the AST side tables.
 */
#include <setjmp.h>
#include "frontend/c/cc.h"
#include "frontend/ast.h"
#include "common/intern.h"
#include "frontend/c/keywords.h"

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

/* ---------------- per-TU state ---------------- */

#define DIR_BITS 11
#define DIR_PAGE (1u << DIR_BITS)
#define NDIR ((1u << VINTERN_ID_BITS) >> DIR_BITS)

/*
 * Lazy function bodies: a static function defined in a header keeps its
 * body as tokens (lztok, spellings of non-identifiers in lzs) and is parsed
 * after the TU only if something names it (SF_REF), like clang's deferred
 * emission one step earlier.  Most of a kernel TU's tokens are such bodies.
 */
typedef struct lztok {
	uint32_t loc;
	uint32_t v;		/* LEX_TOKEN_IDENTIFIER: interned id; otherwise: offset of the spelling in lzs */
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
	/* the AST code generation reads (frontend/ast.h), first: its per-node tables stay in reach of
	   a scaled load and of ldp (doc/quality/MEASURE-ast-split.md); c->syms and c->ast.syms are
	   the same field */
	union {
		struct ast ast;
		struct { AST_FIELDS };
	};
	struct cc_type_name *tyb;	/* tyname/tyname_sugar: 8 buffers in rotation (allocated at the first) */
	unsigned tyk;
	jmp_buf bail;
	int errors;
	struct recov *rec;	/* the innermost parser recovery point (diag.c), NULL: errors end the TU */
	bool for_head;		/* parsing a for (...;...;...) header: clang's form of "expected ';'" */
	int ibr;		/* initializer '{'s open: after an error the skip closes them first */
	const char *dbug;	/* the first delayed bug: an error was due, and must have come by the end */
	uint32_t dbug_loc;
	uint32_t ntypo;		/* typo corrections tried (clang's -fspell-checking-limit) */
	uint32_t miss_loc;	/* where the last "expected ')'" was: one per place */
	struct cc_field_uses *field_uses;	/* VCC_FIELD_USES (field-uses.c): NULL when off */
	uint32_t decl_name, decl_loc, decl_depth;	/* the last declarator's name, where, at which scope depth */

	/* tokens: tk[0] is the current one */
	ptok tk[4];
	int ntk;
	/* the previous token's loc, kind, flags (PP_TOKEN_FLAG_FROM_MACRO: diagnostics), as ptok lays
	   them out: next() copies the 8 bytes with one load and one store */
	union {
		struct {
			uint32_t prevloc;
			uint8_t prevkind, prevflags;
			uint16_t prevctr;
		};
		uint64_t prev8;
	};
	uint32_t dead;		/* inside if (0) ... (and the like): no runtime-behaviour warnings */
	uint8_t defer_rt;	/* parsing a static initializer: -Wdivision-by-zero waits (clang's DiagRuntimeBehavior) */
	uint8_t ndefer;
	struct { uint32_t loc; diag_span r; uint8_t rem; } defer[8];
	uint32_t unk_loc;	/* the last unknown token given -Winvalid-pp-token */
	uint8_t se_direct;	/* the next statement is one of a statement expression's body */
	uint8_t uv_any;		/* -Wuninitialized: 1 the body declares a tracked variable without an initializer, 2 a self-reference, 4 a read before an assignment, 8 a goto or case after a tracked declaration */
	uint8_t nuv;
	uint16_t cnd;
	uint32_t in_proto;
	uint32_t nconv;		/* conversion errors (clang's DefaultError warnings): later checks still run */
	uint32_t constexpr_init;	/* symbol being initialized as a C23 constant object, 0 otherwise */
	bool sie;		/* static_init_ok looks through NF_ERR (after such an error) */
	uint32_t tn_tdef;	/* the last type_name's typedef (sym), 0: none */
	uint32_t *ptag, nptag, cptag;	/* tags declared in the outermost parameter list: a definition's body sees them */	/* parameter declarations being parsed: a new tag there is -Wvisibility */
	uint32_t uv_asg;	/* the variable an '=' being parsed assigns */		/* if/loop/switch bodies being parsed */
	uint32_t uv[64];
	uint16_t uvc[64];	/* ... cnd at the declaration */	/* ... those variables */
	bool after_amp;		/* the unary * being parsed is the operand of & (no -Wvoid-ptr-dereference) */
	uint8_t cv_what;		/* context of the conversion being checked: CX_* (diagnostics) */
	uint32_t cv_loc, cv_fn, cv_arg, cv_callee;	/* cv_callee: CX_PASS: the callee expression's first location */	/* CX_PASS: the callee's sym (0: unknown) and the argument */

	uint32_t *tyhash;	/* interning, open addressing */
	uint32_t tyhcap;
	uint32_t *fnhash, fnhcap, nfn;
	uint32_t *ptrc;		/* qty -> type index of a pointer to it (a cache, checked on use) */
	uint32_t ptr_lo;	/* ptrc entries below are not used (1; a recorded region: its first type) */

	uint32_t cplx[TY_NKINDS];	/* _Complex <kind>: its record type (0: not made yet) */
	diag_span *span;		/* per node: first and last token (start locations), 0: unknown */
	struct cparen { uint16_t l, r; } *paren;	/* per node: its outermost parentheses, bytes before span.b / after span.e (0: none) */

	char *heap, *hp, *hend;	/* bump allocator for everything else */
	struct dop *dops;	/* declarator op stack (frontend/c/declarator.c) */
	uint32_t ndops, capdops;

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
	int cexpr;
	uint32_t *utd, nutd;	/* block-scope typedefs never named: -Wunused-local-typedef at the TU's end */
	uint32_t args_rp;	/* args_list's ')' (where it was expected, if missing) */
	uint32_t fnconv;	/* nconv when the function's body began */
	int fnerr;		/* errors when the function's body began: none since, its unused-variable warnings */
	diag_record **rtq;		/* clang's DiagRuntimeBehavior in a function: emitted at the body's end */
	uint32_t nrtq, rtqcap;
	bool has_depr_tag;
	uint32_t nnparam;	/* parameters declared nonnull so far */
	const char *next_k[16];	/* ext_on's cache: flag (a literal), whether it is on (this TU's options) */
	bool next_v[16];
	uint32_t next_n;
	const char *dms;	/* the next record's diagnosed token, spelled, if a macro may hold it (set just before) */
	diag_fixit dfx;		/* ... and a fix-it for it (s != NULL; machine-applicable), as dms */
	int8_t ext_off;		/* 1: no extension warning is on (command line); -1: not known yet */
	const uint32_t *nenable;	/* pp's "#pragma diagnostic warning|error" count (ext_off holds while 0) */
	int bcp;		/* in __builtin_constant_p's argument: unevaluated, but its names are used */
	attrs pend;		/* attributes before a block-scope declaration */
	int in_struct;		/* parsing a struct/union body */
	int in_tyname;		/* parsing a type name (clang's DSC_type_specifier) */
	int undecl_ty;		/* unknown_type: say clang's "use of undeclared identifier" (block_item's "size_t n = 1;") */
	bool has_pend;
	uint32_t pack[64];	/* #pragma pack stack (bytes, 0: none) */
	uint64_t packid[64];	/* labels (token.c pragma_pack) */
	int npack;
	uint32_t visstk[16];	/* #pragma GCC visibility push: the visibilities (string + 1), innermost last */
	int nvis;
	bool ms_struct;		/* #pragma ms_struct on/off/reset */
	/* #pragma weak (decl.c): waiting for key's declaration (alias: 0, or the weak alias of key) */
	struct pweak { uint32_t key, alias, loc, sym; } *pweak;
	uint32_t npweak, cpweak;	/* (the done aliases, sym set, stay for the end-of-TU check) */

	/* builtin types */
	uint32_t valist_tag;
	uint32_t id_func, id_FUNCTION, id_PRETTY, id_main;
	uint32_t id_int128_t, id_uint128_t, id_valist;
	uint32_t id_null[4];	/* _Nonnull _Nullable _Null_unspecified _Nullable_result */
	uint32_t func_name_sym;
	uint32_t ndiag;		/* diagnostics reported */
	struct driver_declaration_cache_state *dc;	/* declaration cache (declaration-cache.c), NULL: off */

	/* lazy bodies */
	lztok *lzt;
	uint32_t nlzt;
	lzfn *lzf;
	uint32_t nlzf;
	char *lzs;
	uint32_t nlzs;
	bool lz_on;		/* tokens come from lzt[lzc, lze) or lzz (lz_hc), then LEX_TOKEN_EOF */
	bool lz_hc;
	uint32_t lzc, lze;
	pp_lazy lzz;
	uint32_t lz_wm;		/* parsing a lazy body: file-scope syms from here on are hidden (0: none) */

	/* VCC_DRCOV: delivered positions of file-scope declaration starts (UINT64_MAX: lookahead) */
	uint64_t *drb;
	uint32_t ndrb, cdrb;
	/* VCC_DR / VCC_DRREC: per-region declaration replay (frontend/c/replay.c) */
	struct drrec *dr;
	uint8_t dr_on;		/* a region is being recorded: the hooks log */
};
_Static_assert(offsetof(struct cc, ast) == 0, "struct ast is struct cc's first member");
_Static_assert(sizeof(struct ast) < 504, "struct ast stays within ldp's reach");


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
	BI_CPU_SUPPORTS, BI_ARM_RSR, BI_FLT_ROUNDS, BI_OS_LOG_FORMAT, BI_OS_LOG_SIZE,
	/* GNU __atomic_* (memory orders are evaluated, the code is always seq_cst) */
	BI_ATOMIC_LOAD_N, BI_ATOMIC_STORE_N, BI_ATOMIC_EXCHANGE_N, BI_ATOMIC_CMPXCHG_N,
	BI_ATOMIC_LOAD, BI_ATOMIC_STORE, BI_ATOMIC_EXCHANGE, BI_ATOMIC_CMPXCHG,
	BI_ATOMIC_FETCH_ADD, BI_ATOMIC_FETCH_SUB, BI_ATOMIC_FETCH_AND, BI_ATOMIC_FETCH_OR, BI_ATOMIC_FETCH_XOR,
	BI_ATOMIC_FETCH_NAND, BI_ATOMIC_FETCH_MAX, BI_ATOMIC_FETCH_MIN,	/* order as the _FETCH forms (emit: base) */
	BI_ATOMIC_ADD_FETCH, BI_ATOMIC_SUB_FETCH, BI_ATOMIC_AND_FETCH, BI_ATOMIC_OR_FETCH, BI_ATOMIC_XOR_FETCH,
	BI_ATOMIC_NAND_FETCH, BI_ATOMIC_MAX_FETCH, BI_ATOMIC_MIN_FETCH, BI_ATOMIC_TEST_AND_SET, BI_ATOMIC_CLEAR,
	BI_ATOMIC_THREAD_FENCE, BI_ATOMIC_SIGNAL_FENCE, BI_ATOMIC_LOCK_FREE,
	/* legacy __sync_* without an __atomic_* form (the others map onto __atomic_*) */
	BI_SYNC_VAL_CAS, BI_SYNC_BOOL_CAS, BI_SYNC_LOCK_RELEASE, BI_SYNC_SYNCHRONIZE,
	BI_CONJ, BI_CREAL, BI_CIMAG,	/* complex parts (the element type is the signature's return) */
	BI_ROTL8, BI_ROTL16, BI_ROTL32, BI_ROTL64, BI_ROTR8, BI_ROTR16, BI_ROTR32, BI_ROTR64,
	/* ACLE hints and barriers (side effects: BI_ARM_NOP..BI_ARM_TRAP) */
	BI_ARM_NOP, BI_ARM_YIELD, BI_ARM_WFE, BI_ARM_WFI, BI_ARM_SEV, BI_ARM_SEVL, BI_ARM_CLREX,
	BI_ARM_ISB, BI_ARM_DMB, BI_ARM_DSB, BI_ARM_TRAP,
	BI_SETJMP, BI_LONGJMP,	/* GCC's __builtin_setjmp/__builtin_longjmp: buf[0] frame, [1] resume address, [2] sp */
	BI_LIB,	/* plain library call with a prototype */
	BI_NANS, BI_ISSIGNALING,
	BI_ISFPCLASS, BI_ISSUBNORMAL, BI_ISZERO,
	BI_X86_SIMD, BI_X86_PCMPSTR, BI_X86_BLENDVB,
	BI_ELEMENTWISE_ABS,
	BI_STDC_ROTL, BI_STDC_ROTR,
	BI_FAST_SQRT,	/* internal hardware sqrt, full fast-math only */
	BI_FAST_ROOT4,	/* internal double sqrt(sqrt(x)), full fast-math only */
	BI_COMPLEX,
	BI_COUNT
};

enum { CV_NONE, CV_INT, CV_FLT, CV_ADDR, CV_BIGINT };

typedef struct cval {
	int kind;
	uint32_t big, nbig;	/* CV_BIGINT: immutable LE32 storage words in lists */
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
bool cc_init_subobject(cc *c, uint32_t n, uint32_t *init);
/* Constant object representation, little-endian bytes plus a known-bit mask. */
bool cc_bitcast_read(cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data, unsigned char *known);
bool cc_object_size(cc *c, uint32_t pointer, unsigned mode, i128 *size);
void dr_restored(cc *c);
bool cc_is_signed(cc *c, qty t);
/* Kinds for library builtins that the emitter treats specially. Other
 * builtins retain their normal call path. */
enum { CC_LIB_OTHER, CC_LIB_MEMCPY, CC_LIB_MEMMOVE, CC_LIB_MEMSET,
       CC_LIB_STRCMP, CC_LIB_FABSL, CC_LIB_CHK };
int cc_builtin_libkind(int64_t i);
int64_t cc_builtin_plain(int64_t i);
int64_t cc_chk_simplify(int64_t bi, int na, bool osz_const, uint64_t osz, bool len_const, uint64_t len);
const char *cc_builtin_name(int64_t i);
const char *cc_lib_sig(const char *name, int *clang_enc);
uint32_t cc_global_named(cc *c, const char *s);

/* A declaration's target state overrides the translation-unit default. */
static inline x86_features cc_x86_features(const cc *c, uint32_t fn)
{
	if (fn && c->syms[fn].attrs && c->attr[c->syms[fn].attrs].x86_target)
		return c->attr[c->syms[fn].attrs].x86_features;
	if (c->o->x86_features_set) return c->o->x86_features;
	x86_features features = {0};
	x86_cpu_features("x86-64", &features);
	return features;
}

#endif
