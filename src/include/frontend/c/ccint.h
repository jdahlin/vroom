#ifndef VCC_FRONTEND_C_CCINT_H
#define VCC_FRONTEND_C_CCINT_H
/*
 * vcc front end internals shared by frontend/c/ (parser + sema) and codegen/ (code
 * generator): the per-TU state and the AST side tables.
 */
#include <setjmp.h>
#include "frontend/c/cc.h"
#include "common/intern.h"
#include "frontend/c/keywords.h"

#define unlikely(x) __builtin_expect(!!(x), 0)
#define likely(x) __builtin_expect(!!(x), 1)

typedef __int128 i128;
typedef unsigned __int128 u128;

/* ---------------- per-TU state ---------------- */

#define CC_DIRECTORY_BITS 11
#define CC_DIRECTORY_PAGE (1u << CC_DIRECTORY_BITS)
#define CC_DIRECTORY_COUNT ((1u << VINTERN_ID_BITS) >> CC_DIRECTORY_BITS)

typedef struct cstr {		/* a string literal */
	const char *s;		/* bytes, element size each, zero terminated */
	uint32_t n;		/* elements, terminator included */
	uint8_t esz;		/* element size: 1, 2, 4 */
	uint8_t kind;		/* LEX_TOKEN_STRING / LEX_TOKEN_WIDE_STRING / ... */
} cstr;

typedef struct label {
	uint32_t name, loc;
	uint32_t defined;	/* node of the AST_NODE_LABEL, 0 if only referenced */
	uint32_t shadow;	/* __label__ scoping */
	uint32_t depth;
	uint8_t local;		/* declared with __label__ */
	uint8_t addr_taken;
} label;

/* Call-site restrictions kept with declaration attributes, including redeclarations. */
enum {
	CC_ATTRIBUTE_FLAG_TAIL_RETURNS_TWICE = 1u << 0,
	CC_ATTRIBUTE_FLAG_TAIL_DISABLE = 1u << 1,
	CC_ATTRIBUTE_FLAG_TAIL_NOT_CALLED = 1u << 2,
	CC_ATTRIBUTE_FLAG_OPTNONE = 1u << 3,	/* currently prevents the new sibling-call transformation */
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

typedef struct ast_asm_operand {
	uint32_t name;		/* [name] id, 0 */
	uint32_t constraint;	/* string index */
	uint32_t expr;		/* node */
} ast_asm_operand;

typedef struct ast_asm_statement {
	uint32_t tmpl;		/* string index */
	uint32_t nout, nin, nclob, nlabel;
	ast_asm_operand *ops;		/* outputs then inputs */
	uint32_t *clob;		/* string indices */
	uint32_t *labels;	/* label indices */
	uint8_t vol, go, inl, extended;
} ast_asm_statement;

/*
 * Lazy function bodies: a static function defined in a header keeps its
 * body as tokens (lztok, spellings of non-identifiers in lzs) and is parsed
 * after the TU only if something names it (AST_SYMBOL_FLAG_REFERENCED), like clang's deferred
 * emission one step earlier.  Most of a kernel TU's tokens are such bodies.
 */
typedef struct cc_lazy_token {
	uint32_t loc;
	uint32_t v;		/* LEX_TOKEN_IDENTIFIER: interned id; otherwise: offset of the spelling in lzs */
	uint32_t len;
	uint8_t kind, flags;
	uint16_t _pad;
} cc_lazy_token;

typedef struct cc_lazy_function {
	uint32_t sym;
	uint32_t tk, ntk;	/* its tokens in lzt, '{' to '}' */
	uint32_t wm;		/* nsym at the definition: later file-scope syms are not visible */
	uint32_t name, loc, psyms, npsyms, plb, ple;	/* the declarator, for function_def */
	uint8_t done;
	uint8_t hc;		/* the tokens are z's, in a header cache entry (ntk 0) */
	pp_lazy z;
} cc_lazy_function;


struct cc {
	pp *p;
	const cc_options *o;
	struct cc_type_name *tyb;	/* tyname/tyname_sugar: 8 buffers in rotation (allocated at the first) */
	unsigned tyk;
	jmp_buf bail;
	int errors;
	struct cc_recovery_point *rec;	/* the innermost parser recovery point (diag.c), NULL: errors end the TU */
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
	bool sie;		/* static_init_ok looks through AST_NODE_FLAG_ERROR (after such an error) */
	uint32_t tn_tdef;	/* the last type_name's typedef (sym), 0: none */
	uint32_t *ptag, nptag, cptag;	/* tags declared in the outermost parameter list: a definition's body sees them */	/* parameter declarations being parsed: a new tag there is -Wvisibility */
	uint32_t uv_asg;	/* the variable an '=' being parsed assigns */		/* if/loop/switch bodies being parsed */
	uint32_t uv[64];
	uint16_t uvc[64];	/* ... cnd at the declaration */	/* ... those variables */
	bool after_amp;		/* the unary * being parsed is the operand of & (no -Wvoid-ptr-dereference) */
	uint8_t cv_what;		/* context of the conversion being checked: CX_* (diagnostics) */
	uint32_t cv_loc, cv_fn, cv_arg, cv_callee;	/* cv_callee: CX_PASS: the callee expression's first location */	/* CX_PASS: the callee's sym (0: unknown) and the argument */

	ctype *ty;
	uint32_t nty;
	uint32_t *tyhash;	/* interning, open addressing */
	uint32_t tyhcap;
	ast_qualified_type *params;
	uint32_t nparams;
	uint32_t *fnhash, fnhcap, nfn;
	uint32_t *ptrc;		/* qty -> type index of a pointer to it (a cache, checked on use) */
	uint32_t ptr_lo;	/* ptrc entries below are not used (1; a recorded region: its first type) */

	tag *tags;
	uint32_t ntag;
	uint32_t cplx[AST_TYPE_KIND_COUNT];	/* _Complex <kind>: its record type (0: not made yet) */
	sym *syms;
	uint32_t nsym;
	node *nodes;
	diag_span *span;		/* per node: first and last token (start locations), 0: unknown */
	struct cc_node_parentheses { uint16_t l, r; } *paren;	/* per node: its outermost parentheses, bytes before span.b / after span.e (0: none) */
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
	ast_asm_statement *asms;
	uint32_t nasm;

	char *heap, *hp, *hend;	/* bump allocator for everything else */
	struct cc_declarator_operation *dops;	/* declarator op stack (frontend/c/declarator.c) */
	uint32_t ndops, capdops;

	/* scopes: identifier id -> innermost binding (sym index) */
	uint32_t *odir[CC_DIRECTORY_COUNT], *tdir[CC_DIRECTORY_COUNT], *ldir[CC_DIRECTORY_COUNT];
	uint32_t *sstack;	/* syms bound in open scopes */
	uint32_t nsstack;
	uint32_t smark[4096];
	uint32_t depth;

	/* the function being parsed */
	uint32_t fn;		/* sym */
	uint32_t fn_labels;	/* first label of this function */
	uint32_t *switch_cases;	/* AST_NODE_CASE/AST_NODE_DEFAULT nodes of the innermost switch */
	int in_loop, in_switch, in_stmtexpr;
	bool sw_default;	/* the innermost switch has a default label */
	struct cc_switch_case { i128 lo, hi; uint32_t loc; } *cases;	/* the open switches' case labels */
	uint32_t ncase, capcase, case_base;
	ast_qualified_type switch_ty;
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
	ast_qualified_type t_size, t_ptrdiff, t_wchar, t_char16, t_char32, t_valist;
	uint32_t valist_tag;
	uint32_t id_func, id_FUNCTION, id_PRETTY, id_main;
	uint32_t id_int128_t, id_uint128_t, id_valist;
	uint32_t id_null[4];	/* _Nonnull _Nullable _Null_unspecified _Nullable_result */
	uint32_t func_name_sym;
	uint32_t ntoplevel;
	uint32_t *toplevel;	/* file-scope syms in declaration order */
	uint32_t *tlasm;	/* file-scope asm("...") statements (AST_NODE_ASM nodes) */
	uint32_t ntlasm;
	uint32_t ndiag;		/* diagnostics reported */
	struct driver_declaration_cache_state *dc;	/* declaration cache (declaration-cache.c), NULL: off */

	/* lazy bodies */
	cc_lazy_token *lzt;
	uint32_t nlzt;
	cc_lazy_function *lzf;
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


enum {
	CC_BUILTIN_NONE,
	/* special forms (own parsing) */
	CC_BUILTIN_CONSTANT_P, CC_BUILTIN_EXPECT, CC_BUILTIN_EXPECT_PROB, CC_BUILTIN_CHOOSE_EXPR, CC_BUILTIN_TYPES_COMPATIBLE, CC_BUILTIN_OFFSETOF,
	CC_BUILTIN_VA_START, CC_BUILTIN_VA_END, CC_BUILTIN_VA_COPY, CC_BUILTIN_VA_ARG,
	CC_BUILTIN_CLZ, CC_BUILTIN_CLZL, CC_BUILTIN_CLZLL, CC_BUILTIN_CTZ, CC_BUILTIN_CTZL, CC_BUILTIN_CTZLL, CC_BUILTIN_FFS, CC_BUILTIN_FFSL, CC_BUILTIN_FFSLL,
	CC_BUILTIN_POPCOUNT, CC_BUILTIN_POPCOUNTL, CC_BUILTIN_POPCOUNTLL, CC_BUILTIN_PARITY, CC_BUILTIN_PARITYL, CC_BUILTIN_PARITYLL,
	CC_BUILTIN_CLRSB, CC_BUILTIN_CLRSBL, CC_BUILTIN_CLRSBLL, CC_BUILTIN_CLZG, CC_BUILTIN_CTZG, CC_BUILTIN_POPCOUNTG, CC_BUILTIN_SHUFFLEVECTOR, CC_BUILTIN_CONVERTVECTOR,
	CC_BUILTIN_BSWAP16, CC_BUILTIN_BSWAP32, CC_BUILTIN_BSWAP64, CC_BUILTIN_BITREVERSE8, CC_BUILTIN_BITREVERSE16, CC_BUILTIN_BITREVERSE32, CC_BUILTIN_BITREVERSE64,
	CC_BUILTIN_UNREACHABLE, CC_BUILTIN_TRAP, CC_BUILTIN_DEBUGTRAP, CC_BUILTIN_RETURN_ADDRESS, CC_BUILTIN_FRAME_ADDRESS, CC_BUILTIN_EXTRACT_RETURN_ADDRESS,
	CC_BUILTIN_ADD_OVERFLOW, CC_BUILTIN_SUB_OVERFLOW, CC_BUILTIN_MUL_OVERFLOW, CC_BUILTIN_ADDC, CC_BUILTIN_SUBC,
	CC_BUILTIN_OBJECT_SIZE, CC_BUILTIN_DYNAMIC_OBJECT_SIZE, CC_BUILTIN_COUNTED_BY_REF,
	CC_BUILTIN_ALLOCA, CC_BUILTIN_ALLOCA_UNINITIALIZED, CC_BUILTIN_PREFETCH, CC_BUILTIN_ASSUME_ALIGNED, CC_BUILTIN_ASSUME,
	CC_BUILTIN_CLASSIFY_TYPE, CC_BUILTIN_IS_CONSTANT_EVALUATED, CC_BUILTIN_STRLEN,
	CC_BUILTIN_HUGE_VAL, CC_BUILTIN_HUGE_VALF, CC_BUILTIN_INF, CC_BUILTIN_INFF, CC_BUILTIN_NAN, CC_BUILTIN_NANF,
	CC_BUILTIN_ISNAN, CC_BUILTIN_ISINF, CC_BUILTIN_ISFINITE, CC_BUILTIN_SIGNBIT, CC_BUILTIN_FPCLASSIFY, CC_BUILTIN_ISINF_SIGN, CC_BUILTIN_ISNORMAL,
	CC_BUILTIN_ISGREATER, CC_BUILTIN_ISGREATEREQUAL, CC_BUILTIN_ISLESS, CC_BUILTIN_ISLESSEQUAL, CC_BUILTIN_ISLESSGREATER, CC_BUILTIN_ISUNORDERED,
	CC_BUILTIN_CPU_SUPPORTS, CC_BUILTIN_ARM_RSR, CC_BUILTIN_FLT_ROUNDS, CC_BUILTIN_OS_LOG_FORMAT, CC_BUILTIN_OS_LOG_SIZE,
	/* GNU __atomic_* (memory orders are evaluated, the code is always seq_cst) */
	CC_BUILTIN_ATOMIC_LOAD_N, CC_BUILTIN_ATOMIC_STORE_N, CC_BUILTIN_ATOMIC_EXCHANGE_N, CC_BUILTIN_ATOMIC_CMPXCHG_N,
	CC_BUILTIN_ATOMIC_LOAD, CC_BUILTIN_ATOMIC_STORE, CC_BUILTIN_ATOMIC_EXCHANGE, CC_BUILTIN_ATOMIC_CMPXCHG,
	CC_BUILTIN_ATOMIC_FETCH_ADD, CC_BUILTIN_ATOMIC_FETCH_SUB, CC_BUILTIN_ATOMIC_FETCH_AND, CC_BUILTIN_ATOMIC_FETCH_OR, CC_BUILTIN_ATOMIC_FETCH_XOR,
	CC_BUILTIN_ATOMIC_FETCH_NAND, CC_BUILTIN_ATOMIC_FETCH_MAX, CC_BUILTIN_ATOMIC_FETCH_MIN,	/* order as the _FETCH forms (emit: base) */
	CC_BUILTIN_ATOMIC_ADD_FETCH, CC_BUILTIN_ATOMIC_SUB_FETCH, CC_BUILTIN_ATOMIC_AND_FETCH, CC_BUILTIN_ATOMIC_OR_FETCH, CC_BUILTIN_ATOMIC_XOR_FETCH,
	CC_BUILTIN_ATOMIC_NAND_FETCH, CC_BUILTIN_ATOMIC_MAX_FETCH, CC_BUILTIN_ATOMIC_MIN_FETCH, CC_BUILTIN_ATOMIC_TEST_AND_SET, CC_BUILTIN_ATOMIC_CLEAR,
	CC_BUILTIN_ATOMIC_THREAD_FENCE, CC_BUILTIN_ATOMIC_SIGNAL_FENCE, CC_BUILTIN_ATOMIC_LOCK_FREE,
	/* legacy __sync_* without an __atomic_* form (the others map onto __atomic_*) */
	CC_BUILTIN_SYNC_VAL_CAS, CC_BUILTIN_SYNC_BOOL_CAS, CC_BUILTIN_SYNC_LOCK_RELEASE, CC_BUILTIN_SYNC_SYNCHRONIZE,
	CC_BUILTIN_CONJ, CC_BUILTIN_CREAL, CC_BUILTIN_CIMAG,	/* complex parts (the element type is the signature's return) */
	CC_BUILTIN_ROTL8, CC_BUILTIN_ROTL16, CC_BUILTIN_ROTL32, CC_BUILTIN_ROTL64, CC_BUILTIN_ROTR8, CC_BUILTIN_ROTR16, CC_BUILTIN_ROTR32, CC_BUILTIN_ROTR64,
	/* ACLE hints and barriers (side effects: CC_BUILTIN_ARM_NOP..BI_ARM_TRAP) */
	CC_BUILTIN_ARM_NOP, CC_BUILTIN_ARM_YIELD, CC_BUILTIN_ARM_WFE, CC_BUILTIN_ARM_WFI, CC_BUILTIN_ARM_SEV, CC_BUILTIN_ARM_SEVL, CC_BUILTIN_ARM_CLREX,
	CC_BUILTIN_ARM_ISB, CC_BUILTIN_ARM_DMB, CC_BUILTIN_ARM_DSB, CC_BUILTIN_ARM_TRAP,
	CC_BUILTIN_SETJMP, CC_BUILTIN_LONGJMP,	/* GCC's __builtin_setjmp/__builtin_longjmp: buf[0] frame, [1] resume address, [2] sp */
	CC_BUILTIN_LIBRARY,	/* plain library call with a prototype */
	CC_BUILTIN_NANS, CC_BUILTIN_ISSIGNALING,
	CC_BUILTIN_ISFPCLASS, CC_BUILTIN_ISSUBNORMAL, CC_BUILTIN_ISZERO,
	CC_BUILTIN_X86_SIMD, CC_BUILTIN_X86_PCMPSTR, CC_BUILTIN_X86_BLENDVB,
	CC_BUILTIN_ELEMENTWISE_ABS,
	CC_BUILTIN_STDC_ROTL, CC_BUILTIN_STDC_ROTR,
	CC_BUILTIN_FAST_SQRT,	/* internal hardware sqrt, full fast-math only */
	CC_BUILTIN_FAST_ROOT4,	/* internal double sqrt(sqrt(x)), full fast-math only */
	CC_BUILTIN_COMPLEX,
	CC_BUILTIN_COUNT
};

enum { CC_VALUE_NONE, CC_VALUE_INTEGER, CC_VALUE_FLOAT, CC_VALUE_ADDRESS, CC_VALUE_BITINT };

typedef struct cc_value {
	int kind;
	uint32_t big, nbig;	/* CC_VALUE_BITINT: immutable LE32 storage words in lists */
	bool ice;		/* integer constant expression (C11 6.6p6, clang's rules) */
	i128 i;			/* CC_VALUE_INTEGER; CC_VALUE_ADDRESS: byte offset */
	double f;		/* CC_VALUE_FLOAT; with ld, the nearest double of q */
	u128 q;			/* CC_VALUE_FLOAT with ld: the binary128 value (16-byte long double) */
	bool ld;
	uint32_t sym, str;	/* CC_VALUE_ADDRESS base: sym or string literal */
	uint32_t label;		/* CC_VALUE_ADDRESS base: &&label */
	uint32_t sublabel;	/* CC_VALUE_ADDRESS: minus &&sublabel (GNU label difference: a constant once laid out) */
} cc_value;

#define AST_TYPE_OF(c, t) (&(c)->ty[AST_QUALIFIED_TYPE_INDEX(t)])
#define AST_TYPE_KIND_OF(c, t) ((c)->ty[AST_QUALIFIED_TYPE_INDEX(t)].kind)
#define AST_NODE_OF(c, i) (&(c)->nodes[i])

bool cc_eval(cc *c, uint32_t n, cc_value *v);
bool cc_initializer_subobject_find(cc *c, uint32_t n, uint32_t *init);
/* Constant object representation, little-endian bytes plus a known-bit mask. */
bool cc_bit_cast_object_read(cc *c, uint32_t n, uint64_t off, unsigned len, unsigned char *data, unsigned char *known);
bool cc_builtin_object_size(cc *c, uint32_t pointer, unsigned mode, i128 *size);
void cc_replay_restored(cc *c);
bool cc_is_signed(cc *c, ast_qualified_type t);
/* Kinds for library builtins that the emitter treats specially. Other
 * builtins retain their normal call path. */
enum { CC_LIBRARY_KIND_OTHER, CC_LIBRARY_KIND_MEMCPY, CC_LIBRARY_KIND_MEMMOVE, CC_LIBRARY_KIND_MEMSET,
       CC_LIBRARY_KIND_STRCMP, CC_LIBRARY_KIND_FABSL, CC_LIBRARY_KIND_CHECKED };
int cc_builtin_library_kind(int64_t i);
int64_t cc_builtin_unchecked(int64_t i);
int64_t cc_builtin_checked_simplify(int64_t bi, int na, bool osz_const, uint64_t osz, bool len_const, uint64_t len);
const char *cc_builtin_name_get(int64_t i);
const char *cc_builtin_library_signature(const char *name, int *clang_enc);
uint32_t cc_symbol_global_find(cc *c, const char *s);

/* A declaration's target state overrides the translation-unit default. */
static inline x86_features cc_x86_features_get(const cc *c, uint32_t fn)
{
	if (fn && c->syms[fn].attrs && c->attr[c->syms[fn].attrs].x86_target)
		return c->attr[c->syms[fn].attrs].x86_features;
	if (c->o->x86_features_set) return c->o->x86_features;
	x86_features features = {0};
	x86_cpu_features("x86-64", &features);
	return features;
}

#endif
