#ifndef VCC_CC_CCIMPL_H
#define VCC_CC_CCIMPL_H
/* the files of vcc/cc/ share this header: what one of them defines and another uses */
/*
 * vcc front end: parser + sema (see cc.h).
 *
 * One pass, recursive descent, pulling macro-expanded tokens from pp/.
 * Types, records, symbols and nodes are 32-bit indices into per-TU arrays
 * that are reserved (not committed) up front, so nothing is reallocated or
 * copied while parsing.  Semantic analysis is done as each construct is
 * parsed: expression types, implicit conversions (explicit N_CAST nodes),
 * lvalue-ness, and constant evaluation on demand.
 *
 * clang is the reference for every rule that is not plain C11: GNU
 * extensions, record layout (ported from ItaniumRecordLayoutBuilder for
 * the AArch64 target), integer constant expressions vs. foldable ones (null
 * pointer constants depend on the difference: __is_constexpr), and the
 * types of builtins.
 */
#include "../lib/vstack.h"
#include <math.h>
#include <pthread.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include "ccint.h"
#include "../emit/emit.h"
#include "../driver/ccsnap.h"
#include "../lib/arena.h"
#include "../lib/f128.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

#define IDF(id) idlen_(id), idstr_(id)

/* ---------------- types ---------------- */

typedef struct { uint8_t size, align; } basic_ty;

#define T(c, t) (&(c)->ty[QI(t)])
#define KIND(c, t) ((c)->ty[QI(t)].kind)

/* ---------------- tokens ---------------- */

/* per keyword id: a keyword in gnu11, starts a type name, starts a declaration */
enum { KC_KW = 1, KC_TYPE = 2, KC_DECL = 4 };

/* ordinary identifiers and tags; the sym's kind tells which */
enum { S_TAG = 16 };

#define N(i) (&c->nodes[i])

/* lists: append to a temporary vector, then freeze into c->lists */
typedef struct vec {
	uint32_t *v;
	uint32_t n, cap;
	uint32_t inl[16];
} vec;

/* ---------------- builtins ---------------- */

typedef struct builtin {
	const char *name;
	uint8_t id;
	uint8_t constexpr_;	/* folds when its arguments do (ICE if they are) */
	const char *sig;	/* return type then parameters; '.' variadic (see sigtype) */
} builtin;

/* implicit conversion of an rvalue (already decayed) to type to */
enum { CX_NONE, CX_INIT, CX_PASS, CX_RETURN, CX_ASSIGN };

/* ---------------- declaration specifiers ---------------- */

typedef struct dspec {
	qty ty;
	int sc;
	bool inline_, noreturn, thread, auto_type, has_type;
	attrs at;
	uint32_t align;		/* _Alignas */
	uint32_t loc;
	uint32_t tagdecl;	/* struct/union/enum defined or declared here: its tag */
	uint32_t tdef;		/* the typedef name the type came from (sym), 0: none */
	bool tag_bare;		/* "struct S" with no body (a reference or forward declaration) */
} dspec;

typedef struct dop {
	uint8_t kind;		/* 0 ptr, 1 array, 2 func */
	uint8_t q;		/* ptr quals / array param quals */
	uint16_t fl;		/* array: TF_*; func: TF_* */
	uint32_t a, b;		/* array: length lo/hi or vla node; func: params start, count */
	uint32_t np;		/* func: parameter syms (after the types; K&R: no types); array: the size's location */
	bool kr;		/* func: K&R identifier list */
} dop;

typedef struct declr {
	uint32_t name, loc;
	attrs at;
	uint32_t psyms, npsyms;	/* the outermost function declarator's parameter syms (list) */
	uint32_t plb, ple;	/* ... its parameter list: first token, the ')' */
	bool kr;		/* ... from a K&R identifier list (types follow the declarator) */
	bool has_func;
} declr;

/* the declarator op stack (shared, nested declarators push and pop) */
#define DOPS_MAX 4096
#define FM_DEPTH 256	/* nested anonymous members find_member follows */

/* ---------------- struct / union / enum bodies ---------------- */

/* member scratch for struct_body, per thread and kept across TUs: a stack
 * (a nested body pushes its members above the enclosing one's) */
typedef struct fscratch {
	field *fs;
	uint32_t *fa;		/* aligned() per member */
	uint8_t *fp;		/* packed per member */
	uint32_t n, cap;
} fscratch;

/* ---------------- across files ---------------- */

/* tu.c */
VPRIV void *halloc(cc *c, size_t n);
VPRIV void drcov_boundary(cc *c);
VPRIV void dr_read(cc *c, int ns, uint32_t id, uint32_t s);
VPRIV uint32_t dr_intern(cc *c, const ctype *t);
VPRIV void dr_pragma(cc *c, int kind);
VPRIV bool dr_boundary(cc *c);
VPRIV void dr_close(cc *c);
VPRIV qty dr_func(cc *c, qty ret, const qty *par, uint32_t n, int flags);
VPRIV void dr_new(cc *c, uint32_t idx);
VPRIV void dr_tagwrite(cc *c, uint32_t ti);
/* a write to tag ti (or its record type) after its creation: replay.c's provenance must see it */
#define DR_TAGWRITE(c, ti)                       \
	do {                                     \
		if (unlikely((c)->dr != NULL))   \
			dr_tagwrite((c), (ti));  \
	} while (0)
VPRIV void dr_begin(cc *c);
VPRIV void dr_end(cc *c);
/* diag.c */
VPRIV void error_limit(cc *c);
VPRIV void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap);
VPRIV __attribute__((format(printf, 3, 4))) void cc_error(cc *c, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4), noreturn)) void fatal(cc *c, uint32_t loc, const char *fmt, ...);
VPRIV __attribute__((format(printf, 8, 9))) void report(cc *c, const char *what, uint32_t loc, const pp_span *r, int nr, const pp_fixit *fx, int nfx, const char *fmt, ...);
VPRIV __attribute__((format(printf, 3, 4))) void note(cc *c, uint32_t loc, const char *fmt, ...);
extern const char *const w_all[];
extern const char *const w_conv[];
extern const char *const w_selfassign[];
VPRIV void rt_defer(cc *c, const char *flag, const char *const *groups, uint32_t loc, const pp_span *r, int nr,
		    const char *ms, uint32_t mlen, const char *fmt, ...) __attribute__((format(printf, 9, 10)));
VPRIV void rt_flush(cc *c);
VPRIV void unused_value(cc *c, uint32_t e, bool comma);
VPRIV bool unused_cmp(cc *c, uint32_t e);
VPRIV void set_only(cc *c, uint32_t e);
VPRIV __attribute__((format(printf, 10, 11))) bool cc_warnx(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc, const pp_span *r, int nr, const pp_fixit *fx, int nfx, const char *fmt, ...);
VPRIV const char *idname(uint32_t id, int *len);
VPRIV int idlen_(uint32_t id);
VPRIV const char *idstr_(uint32_t id);
/* type.c */
extern const basic_ty basic[TY_PTR];
static inline bool is_int_kind(int k);
static inline bool is_flt_kind(int k);
VPRIV bool is_integer(cc *c, qty t);
VPRIV bool is_float(cc *c, qty t);
VPRIV bool is_arith(cc *c, qty t);
VPRIV bool is_ptr(cc *c, qty t);
VPRIV bool is_scalar(cc *c, qty t);
VPRIV bool is_void(cc *c, qty t);
VPRIV bool is_array(cc *c, qty t);
VPRIV bool is_func(cc *c, qty t);
VPRIV bool is_record(cc *c, qty t);
VPRIV bool is_vector(cc *c, qty t);
VPRIV bool is_complex(cc *c, qty t);
VPRIV bool is_ilagg(cc *c, qty t);
VPRIV qty canon(cc *c, qty t);
VPRIV bool is_signed(cc *c, qty t);
VPRIV uint32_t ty_hash(const ctype *t);
VPRIV uint32_t fn_hash(qty ret, const qty *par, uint32_t n, int flags);
VPRIV uint32_t ty_intern(cc *c, const ctype *t);
VPRIV uint32_t ty_new(cc *c, int kind);
VPRIV qty ty_ptr(cc *c, qty base);
VPRIV qty ty_array(cc *c, qty elem, uint64_t n, int flags);
VPRIV uint64_t arr_len(cc *c, qty t);
VPRIV qty ty_vla(cc *c, qty elem, uint32_t sizenode);
VPRIV qty qualify(cc *c, qty t, int q);
VPRIV qty ty_func(cc *c, qty ret, const qty *par, uint32_t n, int flags);
VPRIV qty ty_vector(cc *c, qty elem, uint32_t size);
VPRIV qty ty_aligned(cc *c, qty t, uint32_t align);
VPRIV qty ty_atomic(cc *c, qty t, uint32_t loc);
static inline qty ty_unatomic(cc *c, qty t);
VPRIV uint64_t ty_size(cc *c, qty t);
VPRIV uint32_t ty_align(cc *c, qty t);
VPRIV bool complete(cc *c, qty t);
VPRIV int rank(cc *c, qty t);
VPRIV qty prim(int k);
VPRIV qty unsigned_of(cc *c, qty t);
VPRIV qty vec_mask_type(cc *c, qty v);
VPRIV qty promote(cc *c, qty t);
VPRIV qty arith_conv(cc *c, qty a, qty b);
VPRIV bool compatible(cc *c, qty a, qty b);
VPRIV qty composite(cc *c, qty a, qty b);
/* layout.c */
VPRIV void lay_record(cc *c, tag *r, const uint32_t *fa, const uint8_t *fp);
/* token.c */
extern uint8_t kwclass[KW_END];
/* identifier id -> the keyword it is in this TU (0: none): C23 spellings map to the
 * GNU ones (bool -> _Bool...), so only -std=c23/gnu23 TUs see them (cc_run sets it) */
extern _Thread_local const uint16_t *kwmap;
extern uint16_t kwmap_gnu[KW_END], kwmap_c23[KW_END];
VPRIV void init_keywords(void);
static inline int kwof(const ptok *t);
VPRIV __attribute__((noinline)) void fetch_directive(cc *c, ptok *t);
VPRIV __attribute__((noinline)) void lz_fetch(cc *c, ptok *t);
static inline __attribute__((always_inline)) void fetch(cc *c, ptok *t);
static inline ptok *tok(cc *c);
static inline bool is_nullability(const cc *c, const ptok *t);
VPRIV ptok *peek(cc *c, int k);
VPRIV __attribute__((noinline)) void next_shift(cc *c);
static inline __attribute__((always_inline)) void next(cc *c);
static inline bool is(cc *c, int kind);
static inline bool iskw(cc *c, int kw);
static inline __attribute__((always_inline)) bool accept(cc *c, int kind);
VPRIV const char *tokspell(const ptok *t, char *buf, size_t cap);
VPRIV __attribute__((noinline, cold, noreturn)) void expect_fail(cc *c, const char *what);
static inline __attribute__((always_inline)) void expect(cc *c, int kind, const char *what);
VPRIV void expect_semi(cc *c, const char *after);
/* sym.c */
VPRIV uint32_t *dslot(cc *c, uint32_t **dir, uint32_t id);
static inline uint32_t dget(uint32_t **dir, uint32_t id);
VPRIV void scope_open(cc *c);
VPRIV void scope_close(cc *c);
VPRIV void unused_statics(cc *c);
VPRIV void unused_typedefs(cc *c);
VPRIV uint32_t sym_new(cc *c, int kind, uint32_t name, qty ty, uint32_t loc);
VPRIV void sym_bind(cc *c, uint32_t s);
VPRIV uint32_t lookup(cc *c, uint32_t id);
VPRIV uint32_t lookup_tag(cc *c, uint32_t id);
static inline bool typedef_id(cc *c, uint32_t id);
VPRIV uint32_t undeclared(cc *c, const char *msg, uint32_t id, uint32_t loc, bool types);
VPRIV bool is_typedef_name(cc *c, const ptok *t);
VPRIV uint32_t mk(cc *c, int op, qty ty, uint32_t loc);
static inline void setspan(cc *c, uint32_t n, uint32_t b);
VPRIV pp_span rng(cc *c, uint32_t n);
VPRIV uint32_t mk2(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b);
VPRIV void vinit(vec *v);
VPRIV void vpush(cc *c, vec *v, uint32_t x);
VPRIV uint32_t vfreeze(cc *c, const vec *v);
VPRIV uint32_t mk_int(cc *c, i128 v, qty ty, uint32_t loc);
VPRIV uint32_t mk_flt(cc *c, double v, qty ty, uint32_t loc);
VPRIV uint32_t mk_fltq(cc *c, u128 q, qty ty, uint32_t loc);
/* lit.c */
VPRIV uint32_t number(cc *c, const ptok *t);
VPRIV bool is_strtok(int k);
VPRIV uint32_t string_lit(cc *c);
VPRIV uint32_t char_lit(cc *c);
/* const.c */
VPRIV i128 trunc_to(cc *c, i128 v, qty t);
VPRIV const field *node_field(cc *c, const node *x);
VPRIV int clz128(u128 x, unsigned bits);
VPRIV bool eval(cc *c, uint32_t n, cval *v);
VPRIV bool is_npc(cc *c, uint32_t n);
VPRIV bool const_int(cc *c, uint32_t n, i128 *out);
/* conv.c */
VPRIV uint32_t cast_node(cc *c, uint32_t n, qty to, int kind);
VPRIV __attribute__((noinline)) uint32_t decay_slow(cc *c, uint32_t n, qty t, int k);
static inline uint32_t decay(cc *c, uint32_t n);
VPRIV const field *bitfield_of(cc *c, uint32_t n);
VPRIV void bitfield_const(cc *c, const field *f, uint32_t v, uint32_t op);
VPRIV bool static_init_ok(cc *c, qty ty, uint32_t init, uint32_t *bad);
VPRIV void deprecated_use(cc *c, uint32_t loc, uint32_t name, const attrs *a);
VPRIV void tag_deprecate(cc *c, uint32_t ti, const attrs *a);
VPRIV void unused_result(cc *c, uint32_t e, bool last);
VPRIV uint32_t expr_loc(cc *c, uint32_t n);
VPRIV qty promoted(cc *c, uint32_t n);
VPRIV int conv_kind(cc *c, qty from, qty to, uint32_t n);
VPRIV bool union_member_for(cc *c, qty u, uint32_t n);
VPRIV bool has_side_effects(cc *c, uint32_t n);
VPRIV uint32_t convert_as(cc *c, uint32_t n, qty to, int what, uint32_t loc);
VPRIV __attribute__((noinline)) uint32_t convert_slow(cc *c, uint32_t n, qty to);
VPRIV void bounds_warn(cc *c, uint32_t n, bool addr);
static inline uint32_t convert(cc *c, uint32_t n, qty to);
VPRIV uint32_t rvalue(cc *c, uint32_t n);
VPRIV void need_scalar(cc *c, uint32_t n, const char *what);
VPRIV bool need_lvalue(cc *c, uint32_t n, uint32_t loc);
VPRIV uint32_t mk_bin(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc);
/* complex.c */
VPRIV qty cplx_elem(cc *c, qty t);
VPRIV qty ty_complex(cc *c, qty et);
VPRIV uint32_t mk_cplx(cc *c, qty t, uint32_t re, uint32_t im, uint32_t loc);
VPRIV uint32_t zero_of(cc *c, qty t, uint32_t loc);
VPRIV uint32_t cplx_reuse(cc *c, uint32_t n, uint32_t *pre);
VPRIV uint32_t with_pre(cc *c, uint32_t pre, uint32_t n);
VPRIV uint32_t cplx_part(cc *c, uint32_t n, int k);
VPRIV uint32_t to_complex(cc *c, uint32_t n, qty t);
VPRIV uint32_t cplx_bool(cc *c, uint32_t n);
VPRIV uint32_t scalar_cond(cc *c, uint32_t n, const char *what);
VPRIV uint32_t neg(cc *c, uint32_t n);
VPRIV void bad_operands(cc *c, uint32_t l, uint32_t r, uint32_t loc);
VPRIV uint32_t cplx_binop(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV uint32_t cplx_unary(cc *c, int op, uint32_t n, uint32_t loc);
/* expr.c */
VPRIV bool is_cmp(int op);
VPRIV uint32_t binop(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV uint32_t assign(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV uint32_t conditional(cc *c, uint32_t cond, uint32_t a, uint32_t b, uint32_t loc);
VPRIV int find_member(cc *c, uint32_t ti, uint32_t name, uint32_t *path, int depth);
VPRIV uint32_t member(cc *c, uint32_t base, uint32_t name, uint32_t loc);
VPRIV uint32_t deref(cc *c, uint32_t p, uint32_t loc);
VPRIV uint32_t addr_of(cc *c, uint32_t n, uint32_t loc);
/* builtin.c */
VPRIV void init_builtins(void);
VPRIV const builtin *builtin_of(uint32_t id);
bool cc_eval(cc *c, uint32_t n, cval *v);
VPRIV bool eval_builtin(cc *c, const node *x, cval *v);
VPRIV uint32_t args_list(cc *c, vec *args);
VPRIV uint32_t mk_builtin(cc *c, int id, qty ty, uint32_t loc, const vec *args);
VPRIV uint32_t vector_builtin(cc *c, bool shuffle, uint32_t loc);
VPRIV uint32_t offsetof_expr(cc *c, uint32_t loc);
VPRIV uint32_t builtin_call(cc *c, const builtin *b, uint32_t loc);
/* expr.c */
VPRIV uint32_t paren_expr(cc *c, uint32_t loc);
VPRIV uint32_t primary(cc *c);
VPRIV __attribute__((noinline)) uint32_t postfix_ops(cc *c, uint32_t n);
static inline uint32_t postfix(cc *c, uint32_t n);
/* exprop.c */
VPRIV uint32_t cast_expr(cc *c);
VPRIV uint32_t cond_expr(cc *c);
VPRIV uint32_t assign_expr(cc *c);
VPRIV uint32_t expr(cc *c);
VPRIV bool const_expr(cc *c, i128 *v, uint32_t *np);
/* attr.c */
VPRIV void init_attrs(void);
VPRIV uint32_t skip_balanced(cc *c);
VPRIV uint32_t string_index(cc *c);
VPRIV int mode_kind(cc *c, uint32_t id, bool sgn);
static inline bool at_attribute(cc *c);
static inline bool at_std_attr(cc *c);
VPRIV __attribute__((noinline)) void attribute_list_(cc *c, attrs *a);
static inline void attribute_list(cc *c, attrs *a);
static inline bool attrs_empty(const attrs *a);
/* the data model: LP64, but ILP32 for wasm32 (long and pointers 4 bytes) */
#define CC_ILP32(c) ((c)->o->target == PP_WASM32)
#define CC_PTRSIZE(c) (CC_ILP32(c) ? 4u : 8u)
/* the 64-bit integer type: long, or long long under ILP32 */
#define CC_I64(c, uns) (CC_ILP32(c) ? ((uns) ? TY_ULLONG : TY_LLONG) : ((uns) ? TY_ULONG : TY_LONG))
VPRIV void merge_attrs(attrs *to, const attrs *from);
/* flow.c */
VPRIV int body_falls_off(cc *c, uint32_t body, uint32_t first_label);
VPRIV void uninit_check(cc *c, uint32_t body, uint32_t first_label);
/* fmt.c */
VPRIV void format_check(cc *c, uint32_t fs, const uint32_t *args, const qty *aty, const uint32_t *atd, uint32_t nargs);
VPRIV void asm_label(cc *c, attrs *a);
/* declspec.c */
VPRIV void unknown_type(cc *c, dspec *ds);
VPRIV bool is_type_start(cc *c, const ptok *t);
VPRIV bool is_decl_start(cc *c, const ptok *t);
VPRIV uint32_t tag_new(cc *c, int kind, uint32_t name, uint32_t loc);
VPRIV void static_assert_decl(cc *c);
VPRIV bool decl_specs(cc *c, dspec *ds, bool allow_sc);
/* declarator.c */
VPRIV void declarator(cc *c, declr *d, bool abstract_ok, uint32_t *first, uint32_t *nops);
VPRIV qty apply_declarator(cc *c, qty t, uint32_t first, uint32_t nops);
VPRIV qty type_name(cc *c);
/* record.c */
extern _Thread_local fscratch tl_fscr;
VPRIV __attribute__((noinline)) void struct_body(cc *c, uint32_t ti);
VPRIV void enum_body(cc *c, uint32_t ti, attrs *after);
/* init.c */
VPRIV uint32_t initializer(cc *c, qty *ty, bool st);
/* decl.c */
VPRIV void apply_sym_attrs(cc *c, sym *s, const attrs *a);
VPRIV uint32_t lib_builtin(cc *c, uint32_t id, uint32_t loc);
VPRIV bool lib_builtin_known(const char *n);
VPRIV const char *lib_builtin_sig(const char *n);
/* mangle.c */
VPRIV void name_overload(cc *c, uint32_t s);
/* decl.c */
VPRIV uint32_t file_scope_sym(cc *c, uint32_t name);
VPRIV void pragma_weak(cc *c, const char *s, const char *e, const char *base, uint32_t loc);
VPRIV void weak_declared(cc *c, uint32_t s);
VPRIV void weak_finish(cc *c);
VPRIV uint32_t implicit_func(cc *c, uint32_t id, uint32_t loc);
VPRIV qty implicit_lib_type(cc *c, uint32_t id);
VPRIV uint32_t implicit_lib_func(cc *c, uint32_t id, uint32_t loc, qty t);
VPRIV qty sig_func_type(cc *c, const char *sig);
VPRIV void declaration(cc *c, vec *out, bool file);
/* stmt.c */
VPRIV uint32_t label_ref(cc *c, uint32_t name, uint32_t loc);
VPRIV uint32_t mkstmt(cc *c, int op, uint32_t loc, uint32_t a, uint32_t b, uint32_t cc_);
VPRIV uint32_t asm_stmt(cc *c, bool toplevel);
VPRIV bool at_asm(cc *c);
VPRIV uint32_t compound_stmt(cc *c, qty *last);
VPRIV uint32_t statement(cc *c);
/* lazy.c */
VPRIV bool lazy_ok(cc *c, uint32_t s, const declr *d);
VPRIV void lazy_capture(cc *c, uint32_t s, const declr *d);
VPRIV void lazy_replayed(const lzfn *L, uint32_t n);
VPRIV void lazy_closure(cc *c);
/* func.c */
VPRIV void function_def(cc *c, uint32_t s, const declr *d);
VPRIV void translation_unit(cc *c);
/* layout.c */
VPRIV const char *tyname(cc *c, qty t);
VPRIV void layout_checks(cc *c);

static inline bool is_int_kind(int k) { return k >= TY_BOOL && k <= TY_UINT128; }
static inline bool is_flt_kind(int k) { return k >= TY_FLOAT16 && k <= TY_LDOUBLE; }
/* long double as binary128 (aarch64 Linux; Apple's is double) */
static inline bool is_q(cc *c, qty t) { return KIND(c, t) == TY_LDOUBLE && c->ty[QI(t)].size == 16; }
/* the binary128 bits of a 16-byte long double constant: the two pool slots after its double */
static inline void fpool_q(cc *c, u128 q) { memcpy(&c->fpool[c->nfpool], &q, 16); c->nfpool += 2; }
static inline u128 fnum_q(const cc *c, uint32_t a) { u128 q; memcpy(&q, &c->fpool[a + 1], 16); return q; }

static inline int kwof(const ptok *t)
{
	return t->kind == T_IDENT && t->id < KW_END ? (int)kwmap[t->id] : 0;
}

/* inline at every token consumer: a call to pp_next and a test; the rest out of line */
static inline __attribute__((always_inline)) void fetch(cc *c, ptok *t)
{
	if (unlikely(c->lz_on))
		lz_fetch(c, t);
	else
		pp_next(c->p, t);
	if (unlikely(t->kind == P_DIRECTIVE))
		fetch_directive(c, t);
}

static inline ptok *tok(cc *c) { return &c->tk[0]; }

/* clang's nullability qualifiers: no meaning for code generation */
static inline bool is_nullability(const cc *c, const ptok *t)
{
	return t->kind == T_IDENT && (t->id == c->id_null[0] || t->id == c->id_null[1] ||
				      t->id == c->id_null[2] || t->id == c->id_null[3]);
}

static inline __attribute__((always_inline)) void next(cc *c)
{
	c->prevloc = c->tk[0].loc;
	c->prevflags = c->tk[0].flags;
	if (unlikely(c->ntk > 1))
		next_shift(c);
	else
		fetch(c, &c->tk[0]);
}

static inline bool is(cc *c, int kind) { return c->tk[0].kind == kind; }
static inline bool iskw(cc *c, int kw) { return kwof(&c->tk[0]) == kw; }

static inline __attribute__((always_inline)) bool accept(cc *c, int kind)
{
	if (c->tk[0].kind != kind)
		return false;
	next(c);
	return true;
}

static inline __attribute__((always_inline)) void expect(cc *c, int kind, const char *what)
{
	if (unlikely(c->tk[0].kind != kind))
		expect_fail(c, what);
	next(c);
}

static inline uint32_t dget(uint32_t **dir, uint32_t id)
{
	uint32_t *pg = dir[id >> DIR_BITS];

	return pg ? pg[id & (DIR_PAGE - 1)] : 0;
}

/* an identifier (not a keyword) */
static inline bool typedef_id(cc *c, uint32_t id)
{
	uint32_t s = lookup(c, id);

	return s && c->syms[s].kind == S_TYPEDEF;
}

/* the source range of node n began at token loc b and ends at the previous token */
static inline void setspan(cc *c, uint32_t n, uint32_t b)
{
	if (n && !c->span[n].e)
		c->span[n] = (pp_span){ b, c->prevloc };
}

/* -Wuninitialized tracks it (clang's isTrackedVar: scalar or vector, not _Atomic) */
static inline bool uv_tracked(cc *c, qty t)
{
	t = canon(c, t);
	if (T(c, t)->flags & TF_ATOMIC)
		return false;
	return is_scalar(c, t) || KIND(c, t) == TY_VECTOR || is_complex(c, t);
}

static inline uint32_t decay(cc *c, uint32_t n)
{
	qty t = N(n)->ty;
	int k = KIND(c, t);

	if (unlikely(N(n)->op == N_INDEX))
		bounds_warn(c, n, false);	/* returns at once for anything else */

	if (likely(k != TY_ARRAY && k != TY_FUNC && !(T(c, t)->flags & TF_ATOMIC)))
		return n;
	return decay_slow(c, n, t, k);
}

/* the value type of an _Atomic(T) (T, qualifiers kept); anything else unchanged */
static inline qty ty_unatomic(cc *c, qty t)
{
	const ctype *x = T(c, t);

	return x->flags & TF_ATOMIC ? QT(x->n, QQ(t)) : t;
}

static inline uint32_t convert(cc *c, uint32_t n, qty to)
{
	to = QUNQ(to);
	if (QI(canon(c, N(n)->ty)) == QI(canon(c, to)) && !bitfield_of(c, n))
		return n;
	return convert_slow(c, n, to);
}

static inline uint32_t postfix(cc *c, uint32_t n)
{
	int k = tok(c)->kind;

	if (likely(k != T_LSQUARE && k != T_LPAREN && k != T_PERIOD && k != T_ARROW && k != T_PLUSPLUS && k != T_MINUSMINUS))
		return n;
	return postfix_ops(c, n);
}

static inline bool at_attribute(cc *c)
{
	int kw = kwof(tok(c));

	return kw == KW___attribute__ || kw == KW___attribute;
}

/* C23 "[[" starts an attribute specifier */
static inline bool at_std_attr(cc *c)
{
	return is(c, T_LSQUARE) && peek(c, 1)->kind == T_LSQUARE;
}

/* most declarators have none: keep the check inline */
static inline void attribute_list(cc *c, attrs *a)
{
	if (at_attribute(c) || at_std_attr(c))
		attribute_list_(c, a);
}

/* no attribute at all (padding excluded) */
/* the u32 fields then the 0/1 flag bytes packed .. noinstr (attr.c asserts the layout) */
static inline bool attrs_empty(const attrs *a)
{
	const unsigned char *p = (const unsigned char *)a;
	unsigned o = a->prio | a->tlsmodel;

	for (size_t i = 0; i <= offsetof(attrs, noinstr); i++)
		o |= p[i];
	return !o;
}

#endif
