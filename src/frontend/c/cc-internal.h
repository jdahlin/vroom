#ifndef VCC_FRONTEND_C_CC_INTERNAL_H
#define VCC_FRONTEND_C_CC_INTERNAL_H
/* the files of vcc/cc/ share this header: what one of them defines and another uses */
/*
 * vcc front end: parser + sema (see cc.h).
 *
 * One pass, recursive descent, pulling macro-expanded tokens from frontend/pp/.
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
#include "port/stack-guard.h"
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

#include "frontend/c/ccint.h"
#include "codegen/emit.h"
#include "driver/declaration-cache.h"
#include "port/job-regions.h"
#include "common/softfloat.h"

/* functions one file defines and another calls: static when the directory is
   compiled as one TU (VCC_UNITY, the default build), so they stay internal */
#ifndef CC_PRIVATE
#ifdef VCC_UNITY
#define CC_PRIVATE static
#else
#define CC_PRIVATE
#endif
#endif

#define CC_IDENTIFIER(id) cc_identifier_length(id), cc_identifier_string(id)

/* ---------------- types ---------------- */

typedef struct { uint8_t size, align; } cc_basic_type_layout;

#define AST_TYPE(c, t) (&(c)->ty[QI(t)])
#define AST_TYPE_KIND(c, t) ((c)->ty[QI(t)].kind)

/* ---------------- tokens ---------------- */

/* per keyword id: a keyword in gnu11, starts a type name, starts a declaration */
enum { CC_KEYWORD_CLASS_KEYWORD = 1, CC_KEYWORD_CLASS_STARTS_TYPE = 2, CC_KEYWORD_CLASS_STARTS_DECLARATION = 4 };

/* ordinary identifiers and tags; the sym's kind tells which */
enum { CC_SYMBOL_NAMESPACE_TAG = 16 };

#define AST_NODE(i) (&c->nodes[i])
#define AST_NODE_IS_ERROR(n) (AST_NODE(n)->flags & NF_ERR)	/* an error node: say nothing more about it */

/* lists: append to a temporary vector, then freeze into c->lists */
typedef struct cc_id_list {
	uint32_t *v;
	uint32_t n, cap;
	uint32_t inl[16];
} cc_id_list;

/* ---------------- builtins ---------------- */

typedef struct cc_builtin {
	const char *name;
	uint8_t id;
	uint8_t flags;		/* bit 0: constexpr; upper bits: CC_LIB_* kind */
	const char *sig;	/* return type then parameters; '.' variadic (see sigtype) */
} cc_builtin;

typedef struct cc_object_bound {
	i128 offset, whole, sub;
	qty storage;
} cc_object_bound;

CC_PRIVATE const cc_builtin *cc_builtin_table_get(void);
CC_PRIVATE uint32_t cc_builtin_atomic_lock_free_build(cc *c, const cc_builtin *b, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_atomic_build(cc *c, const cc_builtin *b, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_rotate_build(cc *c, const cc_builtin *b, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_bit_count_generic(cc *c, int id, uint32_t loc);
CC_PRIVATE bool cc_builtin_object_size_pointer_bound(cc *c, uint32_t n, cc_object_bound *b, unsigned depth, unsigned mode);
CC_PRIVATE uint32_t cc_builtin_os_log_build(cc *c, int id, uint32_t loc);
CC_PRIVATE void cc_builtin_arity_check(cc *c, uint32_t loc, unsigned want, const cc_id_list *args);
CC_PRIVATE qty cc_builtin_signature_type(cc *c, char ch);
CC_PRIVATE qty cc_builtin_type(cc *c, const cc_builtin *b);
CC_PRIVATE uint32_t cc_builtin_elementwise_abs_build(cc *c, const cc_builtin *b, uint32_t loc);
CC_PRIVATE bool cc_builtin_has_effects(cc *c, uint32_t n, bool load);
CC_PRIVATE uint32_t cc_builtin_x86_pcmpstr_build(cc *c, const cc_builtin *b, cc_id_list *args, qty rt, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_x86_simd_build(cc *c, const cc_builtin *b, cc_id_list *args, qty rt, uint32_t loc);
CC_PRIVATE uint32_t cc_lowering_local_new(cc *c, cc_id_list *body, qty t, uint32_t init, uint32_t loc);
CC_PRIVATE void cc_lowering_statement_append(cc *c, cc_id_list *body, uint32_t n, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_vector_lane(cc *c, uint32_t v, qty et, unsigned i, uint32_t loc);

/* implicit conversion of an rvalue (already decayed) to type to */
enum { CC_CONVERSION_CONTEXT_NONE, CC_CONVERSION_CONTEXT_INITIALIZER, CC_CONVERSION_CONTEXT_ARGUMENT, CC_CONVERSION_CONTEXT_RETURN, CC_CONVERSION_CONTEXT_ASSIGNMENT };

/* ---------------- declaration specifiers ---------------- */

typedef struct cc_declaration_specifiers {
	qty ty;
	int sc;
	bool inline_, noreturn, thread, auto_type, has_type, constexpr_;
	bool invalid;		/* an unknown type name stands for the type (int) */
	uint16_t sckw;		/* the storage-class keyword, if any */
	uint32_t sgnloc, longloc, baseloc;	/* signed/unsigned, the first long, short: for clang's errors */
	attrs at;
	uint32_t align;		/* _Alignas */
	uint32_t loc;
	uint32_t tagdecl;	/* struct/union/enum defined or declared here: its tag */
	uint32_t tdef;		/* the typedef name the type came from (sym), 0: none */
	bool tag_bare;		/* "struct S" with no body (a reference or forward declaration) */
	uint32_t tds;		/* the typedef the type was named by (its sym, 0: none), for diagnostics */
} cc_declaration_specifiers;

typedef struct dop {
	uint8_t kind;		/* 0 ptr, 1 array, 2 func */
	uint8_t q;		/* ptr quals / array param quals */
	uint16_t fl;		/* array: TF_*; func: TF_* */
	uint32_t a, b;		/* array: length lo/hi or vla node; func: params start, count */
	uint32_t np;		/* func: parameter syms (after the types; K&R: no types); array: the size's location */
	uint32_t loc;		/* array: opening bracket for element-type diagnostics */
	bool kr;		/* func: K&R identifier list */
} dop;

typedef struct cc_declarator {
	uint32_t name, loc;
	attrs at;
	uint32_t psyms, npsyms;	/* the outermost function declarator's parameter syms (list) */
	uint32_t plb, ple;	/* ... its parameter list: first token, the ')' */
	bool kr;		/* ... from a K&R identifier list (types follow the declarator) */
	bool has_func;
	bool invalid;		/* declare reported its type (a void variable) */
} cc_declarator;

/* the declarator op stack (shared, nested declarators push and pop) */
#define CC_DECLARATOR_OPERATIONS_MAX (1u << 20)
#define CC_MEMBER_FIND_DEPTH_MAX 256	/* nested anonymous members cc_member_find follows */

/* ---------------- struct / union / enum bodies ---------------- */

/* member scratch for cc_record_body_parse, per thread and kept across TUs: a stack
 * (a nested body pushes its members above the enclosing one's) */
typedef struct cc_record_scratch {
	field *fs;
	uint32_t *fa;		/* aligned() per member */
	uint8_t *fp;		/* packed per member */
	uint32_t n, cap;
} cc_record_scratch;

/* ---------------- across files ---------------- */

/* tu.c */
CC_PRIVATE void *cc_heap_allocate(cc *c, size_t n);
CC_PRIVATE void cc_replay_coverage_boundary(cc *c);
CC_PRIVATE void cc_replay_coverage_finish(cc *c);
CC_PRIVATE void cc_replay_coverage_init(void);
CC_PRIVATE bool cc_replay_coverage_is_active(void);
/* replay.c */
CC_PRIVATE void cc_replay_symbol_read(cc *c, int ns, uint32_t id, uint32_t s);
CC_PRIVATE uint32_t cc_replay_type_intern(cc *c, const ctype *t);
CC_PRIVATE void cc_replay_pragma_log(cc *c, int kind);
CC_PRIVATE bool cc_replay_boundary(cc *c);
CC_PRIVATE void cc_replay_region_close(cc *c);
CC_PRIVATE qty cc_replay_type_function(cc *c, qty ret, const qty *par, uint32_t n, int flags);
CC_PRIVATE void cc_replay_type_new_log(cc *c, uint32_t idx);
CC_PRIVATE void cc_replay_tag_write_log(cc *c, uint32_t ti);
/* a write to tag ti (or its record type) after its creation: replay.c's provenance must see it */
#define CC_REPLAY_TAG_WRITE_LOG(c, ti)                       \
	do {                                     \
		if (unlikely((c)->dr != NULL))   \
			cc_replay_tag_write_log((c), (ti));  \
	} while (0)
CC_PRIVATE void cc_replay_region_begin(cc *c);
CC_PRIVATE void cc_replay_region_end(cc *c);
/* diag.c */
CC_PRIVATE void cc_report_error_limit(cc *c);
CC_PRIVATE void diag(cc *c, uint32_t loc, const char *what, const char *fmt, va_list ap);
CC_PRIVATE __attribute__((format(printf, 3, 4))) void cc_report_error(cc *c, uint32_t loc, const char *fmt, ...);
CC_PRIVATE __attribute__((format(printf, 3, 4), noreturn)) void cc_report_fatal(cc *c, uint32_t loc, const char *fmt, ...);
CC_PRIVATE __attribute__((format(printf, 8, 9))) void report(cc *c, const char *what, uint32_t loc, const diag_span *r, int nr, const diag_fixit *fx, int nfx, const char *fmt, ...);
CC_PRIVATE __attribute__((format(printf, 3, 4))) void cc_report_note(cc *c, uint32_t loc, const char *fmt, ...);
extern const char *const cc_warning_group_all[];
CC_PRIVATE bool cc_extension_enabled_slow(cc *c, const char *flag, const char *const *groups);
/* is extension warning flag (one of diag.c's cc_extension_flags) on: one test in the common case */
static inline bool cc_extension_is_enabled(cc *c, const char *flag, const char *const *groups)
{
	if (likely(c->ext_off > 0) && !*c->nenable)
		return false;
	return cc_extension_enabled_slow(c, flag, groups);
}
CC_PRIVATE __attribute__((format(printf, 5, 6))) void cc_report_extension(cc *c, const char *flag, const char *const *groups, uint32_t loc,
							   const char *fmt, ...);
extern const char *const cc_warning_group_gnu[];	/* { "gnu", "pedantic" } */
extern const char *const cc_warning_group_pedantic[];	/* { "pedantic" } */
/* an cc_report_extension that costs a load and a test while it is off */
#define CC_REPORT_EXTENSION(c, flag, groups, loc, ...) \
	do { if (unlikely(cc_extension_is_enabled((c), (flag), (groups)))) cc_report_extension((c), (flag), (groups), (loc), __VA_ARGS__); } while (0)
/* ... at a token spelled ms (from a macro: the caret at the invocation, the notes at the body) */
#define CC_REPORT_EXTENSION_MICROSOFT(c, ms, flag, groups, loc, ...) \
	do { if (unlikely(cc_extension_is_enabled((c), (flag), (groups)))) { (c)->dms = (ms); cc_report_extension((c), (flag), (groups), (loc), __VA_ARGS__); } } while (0)
/* ... the token at loc (from the source, not a macro: tf its flags) with a fix-it removing it */
#define CC_REPORT_EXTENSION_DELETE(c, ms, tf, flag, groups, loc, ...) \
	do { if (unlikely(cc_extension_is_enabled((c), (flag), (groups)))) { (c)->dms = (ms); \
		if (!((tf) & PP_TOKEN_FLAG_FROM_MACRO)) (c)->dfx = (diag_fixit){ (loc), (loc) + (uint32_t)strlen(ms), "", DIAG_APPLICABILITY_MACHINE }; \
		cc_report_extension((c), (flag), (groups), (loc), __VA_ARGS__); } } while (0)
CC_PRIVATE void cc_report_flag_error(cc *c, const char *flag, const char *const *groups, uint32_t loc,
                     const diag_span *r, int nr, const char *fmt, ...) __attribute__((format(printf, 7, 8)));
CC_PRIVATE __attribute__((format(printf, 6, 7))) diag_record *cc_report_warning_begin(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc, const char *fmt, ...);
extern const char *const cc_warning_group_conversion[];
extern const char *const cc_warning_group_self_assign[];
CC_PRIVATE void cc_report_runtime_defer(cc *c, const char *flag, const char *const *groups, uint32_t loc, const diag_span *r, int nr,
		    const char *ms, uint32_t mlen, const char *fmt, ...) __attribute__((format(printf, 9, 10)));
CC_PRIVATE void cc_report_runtime_flush(cc *c);
CC_PRIVATE void cc_report_runtime(cc *c, diag_record *d);
CC_PRIVATE void cc_unused_value_check(cc *c, uint32_t e, bool comma);
CC_PRIVATE bool cc_unused_comparison_check(cc *c, uint32_t e);
CC_PRIVATE void cc_unused_set_only_check(cc *c, uint32_t e);
CC_PRIVATE __attribute__((format(printf, 10, 11))) bool cc_report_warning(cc *c, const char *flag, bool on, const char *const *groups, uint32_t loc, const diag_span *r, int nr, const diag_fixit *fx, int nfx, const char *fmt, ...);
/* field-uses.c: the VCC_FIELD_USES test aid */
enum { FU_DECL, FU_MEMBER, FU_DESIGNATOR, FU_OFFSETOF };
CC_PRIVATE void cc_field_uses_begin(cc *c, const char *selection_path);
CC_PRIVATE __attribute__((noinline, cold)) void cc_field_uses_def(cc *c, uint32_t ti);
CC_PRIVATE __attribute__((noinline, cold)) void cc_field_uses_name_keep(cc *c);
CC_PRIVATE __attribute__((noinline, cold)) void cc_field_uses_decl(cc *c, uint32_t tag, uint32_t idx, uint32_t name, uint32_t loc);
CC_PRIVATE __attribute__((noinline, cold)) void cc_field_uses_member(cc *c, uint32_t n, uint32_t id);
CC_PRIVATE __attribute__((noinline, cold)) void cc_field_uses_path_record(cc *c, int role, uint32_t tag, const uint32_t *path, int depth);
CC_PRIVATE void cc_field_uses_finish(cc *c);
CC_PRIVATE const char *cc_identifier_name(uint32_t id, int *len);
CC_PRIVATE int cc_identifier_length(uint32_t id);
CC_PRIVATE const char *cc_identifier_string(uint32_t id);
/* type.c */
extern const cc_basic_type_layout cc_basic_types[TY_PTR];
static inline bool cc_type_kind_is_integer(int k);
static inline bool cc_type_kind_is_float(int k);
CC_PRIVATE bool cc_type_is_integer(cc *c, qty t);
CC_PRIVATE bool cc_type_is_bitint(cc *c, qty t);
CC_PRIVATE uint32_t cc_bitint_float_cast_build(cc *, uint32_t, qty);
CC_PRIVATE uint32_t cc_builtin_bit_count_bitint(cc *, int, uint32_t, uint32_t, unsigned, uint32_t);
CC_PRIVATE bool cc_bitint_conversion_is_exact(cc *, const cval *, qty, qty);
CC_PRIVATE char *cc_bitint_value_format(cc *, const cval *, qty);
CC_PRIVATE uint32_t cc_bitint_node_new_words(cc *, const uint32_t *, qty, uint32_t);
CC_PRIVATE uint32_t cc_bitint_node_new_integer(cc *, i128, qty, uint32_t);
CC_PRIVATE uint32_t cc_bitint_literal_parse(cc *, const char *, size_t, unsigned, bool, uint32_t);
CC_PRIVATE bool cc_bitint_read_eval(cc *, uint32_t, cval *);
CC_PRIVATE bool cc_bitint_value_is_nonzero(cc *, const cval *);
CC_PRIVATE uint32_t cc_bitint_node_fold(cc *, uint32_t);

CC_PRIVATE bool cc_type_is_float(cc *c, qty t);
CC_PRIVATE bool cc_type_is_arithmetic(cc *c, qty t);
CC_PRIVATE bool cc_type_is_pointer(cc *c, qty t);
CC_PRIVATE bool cc_type_is_scalar(cc *c, qty t);
CC_PRIVATE bool cc_type_is_void(cc *c, qty t);
CC_PRIVATE bool cc_type_is_array(cc *c, qty t);
CC_PRIVATE bool cc_type_is_function(cc *c, qty t);
CC_PRIVATE bool cc_type_is_record(cc *c, qty t);
CC_PRIVATE bool cc_type_is_vector(cc *c, qty t);
CC_PRIVATE bool cc_type_is_bool_vector(cc *c, qty t);
CC_PRIVATE bool cc_type_is_complex(cc *c, qty t);
CC_PRIVATE bool cc_type_is_initializer_aggregate(cc *c, qty t);
CC_PRIVATE qty canon(cc *c, qty t);
CC_PRIVATE bool cc_type_is_signed(cc *c, qty t);
CC_PRIVATE uint32_t cc_type_hash(const ctype *t);
CC_PRIVATE uint32_t cc_type_function_hash(qty ret, const qty *par, uint32_t n, int flags);
CC_PRIVATE uint32_t cc_type_intern(cc *c, const ctype *t);
CC_PRIVATE uint32_t cc_type_new(cc *c, int kind);
CC_PRIVATE qty cc_type_bitint(cc *c, unsigned bits, bool unsign);
CC_PRIVATE unsigned cc_type_integer_bits(cc *c, qty t);
CC_PRIVATE qty cc_type_pointer(cc *c, qty base);
CC_PRIVATE qty cc_type_array(cc *c, qty elem, uint64_t n, int flags);
CC_PRIVATE uint64_t cc_type_array_length(cc *c, qty t);
CC_PRIVATE qty cc_type_vla(cc *c, qty elem, uint32_t sizenode);
CC_PRIVATE qty cc_type_qualify(cc *c, qty t, int q);
CC_PRIVATE qty cc_type_function(cc *c, qty ret, const qty *par, uint32_t n, int flags);
CC_PRIVATE qty cc_type_vector(cc *c, qty elem, uint32_t size);
CC_PRIVATE qty cc_type_aligned(cc *c, qty t, uint32_t align);
CC_PRIVATE qty cc_type_atomic(cc *c, qty t, uint32_t loc);
static inline qty cc_type_unatomic(cc *c, qty t);
CC_PRIVATE uint64_t cc_type_size(cc *c, qty t);
CC_PRIVATE uint32_t cc_type_alignment(cc *c, qty t);
CC_PRIVATE bool cc_type_complete(cc *c, qty t);
CC_PRIVATE int cc_type_rank(cc *c, qty t);
CC_PRIVATE qty cc_type_basic(int k);
CC_PRIVATE qty cc_type_unsigned(cc *c, qty t);
CC_PRIVATE qty cc_type_vector_mask(cc *c, qty v);
CC_PRIVATE qty cc_type_promote(cc *c, qty t);
CC_PRIVATE qty cc_type_arithmetic_conversion(cc *c, qty a, qty b);
CC_PRIVATE bool cc_type_is_compatible(cc *c, qty a, qty b);
CC_PRIVATE qty cc_type_composite(cc *c, qty a, qty b);
/* record-layout.c */
CC_PRIVATE void cc_layout_record(cc *c, tag *r, const uint32_t *fa, const uint8_t *fp);
/* token.c */
extern uint8_t cc_keyword_class[KW_END];
/* identifier id -> the keyword it is in this TU (0: none): C23 spellings map to the
 * GNU ones (bool -> _Bool...), so only -std=c23/gnu23 TUs see them (cc_run sets it) */
extern _Thread_local const uint16_t *cc_keyword_map;
extern uint16_t cc_keyword_map_gnu[KW_END], cc_keyword_map_c23[KW_END], cc_keyword_map_iso[KW_END];
CC_PRIVATE void cc_keyword_table_init(void);
static inline int cc_token_keyword(const ptok *t);
CC_PRIVATE __attribute__((noinline)) void cc_token_fetch_directive(cc *c, ptok *t);
CC_PRIVATE __attribute__((noinline)) void cc_token_fetch_lazy(cc *c, ptok *t);
static inline __attribute__((always_inline)) void cc_token_fetch(cc *c, ptok *t);
static inline ptok *tok(cc *c);
static inline bool cc_token_is_nullability(const cc *c, const ptok *t);
CC_PRIVATE ptok *cc_token_peek(cc *c, int k);
CC_PRIVATE __attribute__((noinline)) void cc_token_next_shift(cc *c);
static inline __attribute__((always_inline)) void next(cc *c);
static inline bool cc_token_is(cc *c, int kind);
static inline bool cc_token_is_keyword(cc *c, int kw);
static inline __attribute__((always_inline)) bool cc_token_accept(cc *c, int kind);
CC_PRIVATE const char *cc_token_spelling(const ptok *t, char *buf, size_t cap);
CC_PRIVATE __attribute__((noinline, cold, noreturn)) void cc_token_expect_fail(cc *c, const char *what);
CC_PRIVATE __attribute__((noinline, cold, noreturn)) void cc_token_expect_left_paren_fail(cc *c, const char *kw);
CC_PRIVATE __attribute__((noinline, cold, noreturn)) void cc_token_expect_here_fail(cc *c, const char *what);
/* diag.c: parser recovery.  A recovery point in a block, a struct body or at file
   scope: a parse error (fatal, a failed expect) reports, jumps back to the innermost
   one, which restores the parser's state and skips to the end of the statement or
   declaration (GCC's c_parser_skip_to_end_of_block_or_statement); parsing goes on.
   One _setjmp per block, none per statement.  CC_RECOVERY_ARM is true after a jump. */
typedef struct recov {
	jmp_buf jb;
	struct recov *prev;
	uint32_t depth, ndops, ncase, case_base, dead, fn, constexpr_init;
	int in_loop, in_switch, in_stmtexpr, unevaluated, in_struct, ibr, in_tyname;
	uint32_t *switch_cases;
	qty switch_ty;
	bool sw_default, has_pend, for_head;
	uint32_t again;		/* the token of the last recovery: an error there again skips it */
} recov;
CC_PRIVATE void cc_recovery_save(cc *c, recov *r);
CC_PRIVATE void cc_recovery_skip_statement(cc *c, bool top);
CC_PRIVATE void cc_recovery_skip_semicolon(cc *c);
CC_PRIVATE __attribute__((noreturn)) void cc_recovery_jump(cc *c);
CC_PRIVATE __attribute__((noinline, cold)) void cc_recovery_expect_match_fail(cc *c, int kind, uint32_t open);
CC_PRIVATE void cc_recovery_skip_close(cc *c, int kind);
static inline __attribute__((always_inline)) void cc_token_expect_match(cc *c, int k, uint32_t open);
enum { CC_RECOVERY_BLOCK, CC_RECOVERY_TOP_LEVEL, CC_RECOVERY_FOR_HEAD };
CC_PRIVATE void cc_recovery_restore(cc *c, recov *r, int mode);
#define CC_RECOVERY_ARM(c, r) (cc_recovery_save(c, r), _setjmp((r)->jb))
#define CC_RECOVERY_POP(c, r) ((c)->rec = (r)->prev)
static inline __attribute__((always_inline)) void cc_token_expect(cc *c, int kind, const char *what);
CC_PRIVATE bool cc_token_expect_semicolon(cc *c, const char *after);
CC_PRIVATE bool cc_token_is_not_expression_start(cc *c, const ptok *t);
CC_PRIVATE bool cc_declaration_comma_typo(cc *c);
CC_PRIVATE void cc_pragma_parse(cc *c, const ptok *t);
/* scope.c, node.c, typo-correction.c, unused-declaration.c */
CC_PRIVATE uint32_t *cc_scope_slot(cc *c, uint32_t **dir, uint32_t id);
static inline uint32_t cc_scope_get(uint32_t **dir, uint32_t id);
CC_PRIVATE void cc_scope_open(cc *c);
CC_PRIVATE void cc_scope_close(cc *c);
CC_PRIVATE void cc_scope_close_prototype(cc *c);
CC_PRIVATE void cc_scope_prototype_bindings_save(cc *c, uint32_t s);
CC_PRIVATE void cc_unused_statics_report(cc *c);
CC_PRIVATE void cc_unused_typedefs_report(cc *c);
CC_PRIVATE void cc_lint_naming_report(cc *c);	/* naming-lint.c: vpp --lint */
CC_PRIVATE uint32_t cc_symbol_new(cc *c, int kind, uint32_t name, qty ty, uint32_t loc);
CC_PRIVATE void cc_symbol_bind(cc *c, uint32_t s);
CC_PRIVATE uint32_t cc_symbol_lookup(cc *c, uint32_t id);
CC_PRIVATE uint32_t cc_tag_lookup(cc *c, uint32_t id);
static inline bool cc_symbol_is_typedef_identifier(cc *c, uint32_t id);
enum { CC_TYPO_KEYWORDS_TYPE = 1, CC_TYPO_KEYWORDS_STATEMENT = 2 };	/* keywords offered as typo corrections */
CC_PRIVATE uint32_t cc_undeclared_report(cc *c, const char *msg, uint32_t id, uint32_t loc, bool types, int kws, uint32_t *kw);
CC_PRIVATE uint32_t cc_typo_keyword_find(cc *c, uint32_t id, int kws);
CC_PRIVATE bool cc_token_is_typedef_name(cc *c, const ptok *t);
CC_PRIVATE uint32_t cc_node_new(cc *c, int op, qty ty, uint32_t loc);
static inline void cc_node_span_set(cc *c, uint32_t n, uint32_t b);
CC_PRIVATE diag_span cc_node_range(cc *c, uint32_t n);
CC_PRIVATE diag_span cc_node_range_bare(cc *c, uint32_t n);	/* without the parentheses */
CC_PRIVATE void cc_report_forward_note(cc *c, qty t);
CC_PRIVATE uint32_t cc_node_new_operands(cc *c, int op, qty ty, uint32_t loc, uint32_t a, uint32_t b);
CC_PRIVATE void cc_id_list_init(cc_id_list *v);
CC_PRIVATE void cc_id_list_append(cc *c, cc_id_list *v, uint32_t x);
CC_PRIVATE uint32_t cc_id_list_freeze(cc *c, const cc_id_list *v);
CC_PRIVATE uint32_t cc_node_integer_new(cc *c, i128 v, qty ty, uint32_t loc);
CC_PRIVATE uint32_t cc_node_error_new(cc *c, uint32_t loc);
/* rustc's delay_bug (PLAN-diag D2.1): code that goes quiet because an error was
   reported (an error node, an invalid declaration) says so; a TU that ends
   with no error reports "internal error" instead of compiling wrongly */
CC_PRIVATE __attribute__((noinline, cold)) void cc_report_delayed_bug(cc *c, uint32_t loc, const char *what);
CC_PRIVATE void cc_report_delayed_bug_check(cc *c);
CC_PRIVATE uint32_t cc_node_float_new(cc *c, double v, qty ty, uint32_t loc);
CC_PRIVATE uint32_t cc_node_float128_new(cc *c, u128 q, qty ty, uint32_t loc);
CC_PRIVATE void cc_unused_variables_report(cc *c, uint32_t m);
/* literal.c */
CC_PRIVATE uint32_t cc_literal_number_parse(cc *c, const ptok *t);
CC_PRIVATE bool cc_token_kind_is_string(int k);
CC_PRIVATE uint32_t cc_literal_string_parse(cc *c);
CC_PRIVATE uint32_t cc_literal_character_build(cc *c);
/* constant-eval.c, object-representation.c */
CC_PRIVATE i128 cc_constant_truncate(cc *c, i128 v, qty t);
CC_PRIVATE bool cc_report_runtime_is_quiet(const cc *c);
CC_PRIVATE void cc_format_i128(char *b, size_t n, i128 v);
CC_PRIVATE const field *cc_node_member_field(cc *c, const node *x);
CC_PRIVATE int cc_count_leading_zeros_128(u128 x, unsigned bits);
CC_PRIVATE bool cc_constant_eval(cc *c, uint32_t n, cval *v);
CC_PRIVATE uint32_t cc_bit_cast_build(cc *c, uint32_t loc);
CC_PRIVATE bool cc_bit_cast_value(cc *c, uint32_t n, cval *v);
CC_PRIVATE bool cc_bit_cast_is_constant(cc *c, uint32_t n);
CC_PRIVATE bool cc_bit_cast_vector_is_constant(cc *c, uint32_t n);
CC_PRIVATE bool cc_expression_is_null_pointer_constant(cc *c, uint32_t n);
CC_PRIVATE bool cc_constant_integer(cc *c, uint32_t n, i128 *out);
/* conversion.c, conversion-warnings.c, array-bounds.c */
CC_PRIVATE uint32_t cc_cast_insert(cc *c, uint32_t n, qty to, int kind);
CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_decay_slow(cc *c, uint32_t n, qty t, int k);
static inline uint32_t cc_conversion_decay(cc *c, uint32_t n);
CC_PRIVATE const field *cc_node_bitfield(cc *c, uint32_t n);
CC_PRIVATE void cc_conversion_bitfield_constant_check(cc *c, const field *f, uint32_t v, uint32_t op);
CC_PRIVATE bool cc_constexpr_initializer_is_ok(cc *c, uint32_t init);
CC_PRIVATE void cc_constexpr_conversion_check(cc *c, uint32_t n, qty to);
CC_PRIVATE bool cc_constexpr_type_is_valid(cc *c, qty t);
CC_PRIVATE bool cc_initializer_static_is_ok(cc *c, qty ty, uint32_t init, uint32_t *bad);
CC_PRIVATE void cc_deprecated_use_report(cc *c, uint32_t loc, uint32_t name, const attrs *a, uint32_t dloc);
CC_PRIVATE void cc_tag_deprecate(cc *c, uint32_t ti, const attrs *a);
CC_PRIVATE uint32_t cc_attribute_new(cc *c, const attrs *a);
CC_PRIVATE void cc_unused_result_check(cc *c, uint32_t e, bool last);
CC_PRIVATE uint32_t cc_expression_location(cc *c, uint32_t n);
CC_PRIVATE qty cc_node_promoted_type(cc *c, uint32_t n);
CC_PRIVATE int cc_conversion_cast_kind(cc *c, qty from, qty to, uint32_t n);
CC_PRIVATE bool cc_conversion_is_union_target(cc *c, qty u, uint32_t n);
CC_PRIVATE bool cc_expression_has_side_effects(cc *c, uint32_t n);
CC_PRIVATE uint32_t cc_conversion_apply_as(cc *c, uint32_t n, qty to, int what, uint32_t loc);
CC_PRIVATE __attribute__((noinline)) uint32_t cc_conversion_apply_slow(cc *c, uint32_t n, qty to);
CC_PRIVATE void cc_array_bounds_warn(cc *c, uint32_t n, bool addr);
static inline uint32_t cc_conversion_apply(cc *c, uint32_t n, qty to);
CC_PRIVATE uint32_t cc_node_rvalue(cc *c, uint32_t n);
CC_PRIVATE void cc_node_require_scalar(cc *c, uint32_t n, const char *what);
CC_PRIVATE bool cc_node_require_lvalue(cc *c, uint32_t n, uint32_t loc);
CC_PRIVATE uint32_t cc_node_binary_new(cc *c, int op, qty ty, uint32_t a, uint32_t b, uint32_t loc);
CC_PRIVATE void cc_conversion_invalid_report(cc *c, uint32_t n, qty to);
CC_PRIVATE uint32_t cc_conversion_check(cc *c, uint32_t n, qty to);
CC_PRIVATE void cc_conversion_constant_check(cc *c, uint32_t n, qty to);
CC_PRIVATE const char *cc_type_array_name(cc *c, qty t);
/* complex.c, lowering.c */
CC_PRIVATE qty cc_complex_element_type(cc *c, qty t);
CC_PRIVATE qty cc_type_complex(cc *c, qty et);
CC_PRIVATE uint32_t cc_complex_node_new(cc *c, qty t, uint32_t re, uint32_t im, uint32_t loc);
CC_PRIVATE uint32_t cc_lowering_zero(cc *c, qty t, uint32_t loc);
CC_PRIVATE uint32_t cc_lowering_once(cc *c, uint32_t n, uint32_t *pre);
CC_PRIVATE uint32_t cc_lowering_sequence(cc *c, uint32_t pre, uint32_t n);
CC_PRIVATE uint32_t cc_complex_part(cc *c, uint32_t n, int k);
CC_PRIVATE uint32_t cc_complex_convert(cc *c, uint32_t n, qty t);
CC_PRIVATE uint32_t cc_complex_to_bool(cc *c, uint32_t n);
CC_PRIVATE uint32_t cc_condition_scalar(cc *c, uint32_t n, const char *what);
CC_PRIVATE uint32_t neg(cc *c, uint32_t n);
CC_PRIVATE void cc_operands_invalid_report(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE const char *cc_punctuator_spelling(int k);
CC_PRIVATE uint32_t cc_complex_binary(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE uint32_t cc_atomic_wide_lock_free_runtime_build(cc *c, cc_id_list *args, uint32_t loc);
CC_PRIVATE bool cc_atomic_wide_type_is_wide(cc *c, qty t);
CC_PRIVATE uint32_t cc_atomic_wide_load_build(cc *c, uint32_t n);
CC_PRIVATE uint32_t cc_atomic_wide_assignment_build(cc *c, uint32_t lhs, uint32_t rhs, int op, bool post, uint32_t loc);
CC_PRIVATE uint32_t cc_atomic_wide_builtin_build(cc *c, int id, qty t, const cc_id_list *input, uint32_t loc);
CC_PRIVATE uint32_t cc_complex_increment(cc *c, uint32_t n, int op, uint32_t loc);
CC_PRIVATE uint32_t cc_complex_unary(cc *c, int op, uint32_t n, uint32_t loc);
CC_PRIVATE bool cc_lowering_is_pure(cc *c, uint32_t n, int d);
/* expression.c */
CC_PRIVATE bool cc_operator_is_comparison(int op);
CC_PRIVATE uint32_t cc_binary_build(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE uint32_t cc_assignment_build(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE uint32_t cc_conditional_build(cc *c, uint32_t cond, uint32_t a, uint32_t b, uint32_t loc);
CC_PRIVATE int cc_member_find(cc *c, uint32_t ti, uint32_t name, uint32_t *path, int depth);
CC_PRIVATE uint32_t member(cc *c, uint32_t base, uint32_t name, uint32_t loc);
CC_PRIVATE uint32_t cc_dereference_build(cc *c, uint32_t p, uint32_t loc);
CC_PRIVATE uint32_t cc_address_of_build(cc *c, uint32_t n, uint32_t loc);
/* builtin.c */
CC_PRIVATE void cc_builtin_table_init(void);
CC_PRIVATE const cc_builtin *cc_builtin_lookup(uint32_t id);
bool cc_eval(cc *c, uint32_t n, cval *v);
CC_PRIVATE bool cc_builtin_fold(cc *c, const node *x, cval *v);
CC_PRIVATE uint32_t cc_builtin_arguments_parse(cc *c, cc_id_list *args);
CC_PRIVATE uint32_t cc_builtin_node_new(cc *c, int id, qty ty, uint32_t loc, const cc_id_list *args);
CC_PRIVATE uint32_t cc_builtin_vector_build(cc *c, bool shuffle, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_offsetof_build(cc *c, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_fast_math_build(cc *c, const char *name, size_t len, qty ft, const cc_id_list *args, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_fp_sign_build(cc *c, const char *name, size_t len, qty ft, const cc_id_list *args, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_narrow_math_build(cc *c, const char *name, size_t len, uint32_t arg, uint32_t loc);
CC_PRIVATE uint32_t cc_builtin_call_build(cc *c, const cc_builtin *b, uint32_t loc);
/* expression.c, call.c */
CC_PRIVATE uint32_t cc_parenthesized_parse(cc *c, uint32_t loc);
CC_PRIVATE uint32_t cc_primary_parse(cc *c);
CC_PRIVATE __attribute__((noinline)) uint32_t cc_postfix_operators_parse(cc *c, uint32_t n);
static inline uint32_t cc_postfix_parse(cc *c, uint32_t n);
CC_PRIVATE __attribute__((noinline)) uint32_t cc_overload_pick(cc *c, uint32_t head, const cc_id_list *args, uint32_t loc);
CC_PRIVATE __attribute__((noinline)) uint32_t call(cc *c, uint32_t fn, uint32_t loc);
/* expression-parse.c, expression-warnings.c */
CC_PRIVATE uint32_t cc_vla_bounds_capture(cc *c, qty ty, uint32_t first, uint32_t value, uint32_t loc);
CC_PRIVATE uint32_t cc_cast_expression_parse(cc *c);
CC_PRIVATE uint32_t cc_conditional_parse(cc *c);
CC_PRIVATE uint32_t cc_assignment_parse(cc *c);
CC_PRIVATE uint32_t expr(cc *c);
CC_PRIVATE bool cc_constant_expression_parse(cc *c, i128 *v, uint32_t *np);
CC_PRIVATE void cc_expression_runtime_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE uint32_t cc_node_without_implicit(cc *c, uint32_t n);
CC_PRIVATE bool cc_expression_is_same_reference(cc *c, uint32_t a, uint32_t b, int depth);
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_self_compare_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_null_compare_warn(cc *c, int op, uint32_t x, uint32_t nul);
CC_PRIVATE __attribute__((cold, noinline)) void cc_expression_range_compare_warn(cc *c, uint32_t n, int nk, uint32_t loc);
CC_PRIVATE void cc_expression_logical_constant_check(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc);
CC_PRIVATE void cc_expression_overflow_warn(cc *c, int op, uint32_t n, uint32_t loc, uint32_t sub0, uint32_t sub1);
CC_PRIVATE void cc_expression_self_assign_warn(cc *c, uint32_t l, uint32_t r, uint32_t loc);
/* attr.c */
CC_PRIVATE void cc_attribute_table_init(void);
CC_PRIVATE uint32_t cc_token_skip_balanced(cc *c);
CC_PRIVATE uint32_t cc_attribute_string_parse(cc *c, uint32_t attr);
CC_PRIVATE int cc_attribute_mode_kind(cc *c, uint32_t id, bool sgn);
static inline bool cc_token_at_attribute(cc *c);
static inline bool cc_token_at_standard_attribute(cc *c);
CC_PRIVATE __attribute__((noinline)) void cc_attribute_list_parse_slow(cc *c, attrs *a);
static inline void cc_attribute_list_parse(cc *c, attrs *a);
static inline bool cc_attribute_is_empty(const attrs *a);
/* the data model: LP64, but ILP32 for wasm32 (long and pointers 4 bytes) */
#define CC_ILP32(c) ((c)->o->target == PP_WASM32)
#define CC_PTRSIZE(c) (CC_ILP32(c) ? 4u : 8u)
/* the 64-bit integer type: long, or long long under ILP32 */
#define CC_I64(c, uns) (CC_ILP32(c) ? ((uns) ? TY_ULLONG : TY_LLONG) : ((uns) ? TY_ULONG : TY_LONG))
CC_PRIVATE void cc_attribute_merge(attrs *to, const attrs *from);
/* flow.c: reach.c */
typedef struct cc_reach_state {
	cc *c;
	uint8_t *lab;		/* label reached, from first */
	uint32_t first, nlab;
	bool changed;
	bool brk, cont;		/* the innermost loop/switch: reached by break/continue */
	bool sw_in;		/* the innermost switch: its condition reached */
	bool sw_const;		/* ... with a constant condition */
	i128 sw_val;
	bool sw_hit;		/* ... that some case matches */
	bool live_ret, fake;	/* a reachable return; a reachable noreturn call */
} cc_reach_state;
CC_PRIVATE int cc_reach_condition_constant(cc_reach_state *f, uint32_t n);
CC_PRIVATE bool cc_reach_is_noreturn(cc_reach_state *f, uint32_t e);
CC_PRIVATE bool cc_reach_case_matches(cc_reach_state *f, uint32_t n, i128 v);
CC_PRIVATE bool cc_reach_has_default(cc_reach_state *f, uint32_t n);
CC_PRIVATE int cc_reach_body_falls_off(cc *c, uint32_t body, uint32_t first_label);
CC_PRIVATE void cc_uninitialized_check(cc *c, uint32_t body, uint32_t first_label);
/* fmt.c */
CC_PRIVATE uint32_t cc_format_check_attribute(cc *c, uint32_t fs);
CC_PRIVATE uint32_t cc_typedef_sugar_expression(cc *c, uint32_t n);
CC_PRIVATE void cc_format_check(cc *c, uint32_t fs, const uint32_t *args, const qty *aty, const uint32_t *atd, uint32_t nargs);
CC_PRIVATE void cc_typo_c23_word_help(cc *c, diag_record *d, uint32_t id);
CC_PRIVATE void cc_asm_label_parse(cc *c, attrs *a);
CC_PRIVATE void cc_x86_target_attribute_parse(cc *c, attrs *a, uint32_t str, uint32_t loc);
/* declspec.c */
CC_PRIVATE void cc_declaration_specifiers_unknown_type_report(cc *c, cc_declaration_specifiers *ds, bool allow_sc);
CC_PRIVATE bool cc_token_is_type_start(cc *c, const ptok *t);
CC_PRIVATE bool cc_token_is_declaration_start(cc *c, const ptok *t);
CC_PRIVATE uint32_t cc_tag_new(cc *c, int kind, uint32_t name, uint32_t loc);
CC_PRIVATE void cc_static_assert_parse(cc *c);
CC_PRIVATE bool cc_declaration_specifiers_parse(cc *c, cc_declaration_specifiers *ds, bool allow_sc);
/* declarator.c */
CC_PRIVATE void cc_declarator_parse(cc *c, cc_declarator *d, bool abstract_ok, uint32_t *first, uint32_t *nops);
CC_PRIVATE qty cc_declarator_vector_attribute_apply(cc *c, qty t, uint32_t size, uint32_t loc);
CC_PRIVATE qty cc_declarator_apply(cc *c, qty t, uint32_t first, uint32_t nops);
CC_PRIVATE qty cc_type_name_parse(cc *c);
/* record-parse.c */
extern _Thread_local cc_record_scratch cc_record_scratch_thread;
CC_PRIVATE __attribute__((noinline)) void cc_record_body_parse(cc *c, uint32_t ti);
CC_PRIVATE void cc_enum_body_parse(cc *c, uint32_t ti, attrs *after, uint32_t loc);
/* init.c */
CC_PRIVATE uint32_t cc_initializer_parse(cc *c, qty *ty, bool st);
/* decl.c */
CC_PRIVATE void cc_attribute_apply_symbol(cc *c, sym *s, const attrs *a);
CC_PRIVATE uint32_t cc_library_builtin_declare(cc *c, uint32_t id, uint32_t loc);
CC_PRIVATE bool cc_library_builtin_is_known(const char *n);
CC_PRIVATE const char *cc_library_builtin_signature(const char *n);
/* mangle.c */
CC_PRIVATE void cc_mangle_overload_name_set(cc *c, uint32_t s);
/* decl.c */
CC_PRIVATE uint32_t cc_symbol_file_scope_find(cc *c, uint32_t name);
CC_PRIVATE void cc_pragma_weak_parse(cc *c, const char *s, const char *e, const char *base, uint32_t loc);
CC_PRIVATE void cc_pragma_weak_declared(cc *c, uint32_t s);
CC_PRIVATE const char *cc_conversion_char_signedness(cc *c, qty t);
CC_PRIVATE int cc_token_semicolon_approximate(cc *c);
CC_PRIVATE void cc_pragma_weak_finish(cc *c);
CC_PRIVATE uint32_t cc_implicit_function_declare(cc *c, uint32_t id, uint32_t loc);
CC_PRIVATE uint32_t cc_library_function_declare(cc *c, uint32_t id, uint32_t loc);
CC_PRIVATE const char *cc_library_header(cc *c, uint32_t id);
CC_PRIVATE void cc_library_include_fixit(cc *c, diag_record *d, uint32_t loc, const char *hdr);
CC_PRIVATE qty cc_library_implicit_type(cc *c, uint32_t id);
CC_PRIVATE uint32_t cc_library_implicit_declare(cc *c, uint32_t id, uint32_t loc, qty t);
CC_PRIVATE qty cc_builtin_signature_function_type(cc *c, const char *sig);
CC_PRIVATE uint32_t cc_builtin_strncmp_zero_fold(cc *c, const cc_id_list *args, uint32_t loc);
CC_PRIVATE void cc_declaration_parse(cc *c, cc_id_list *out, bool file);
CC_PRIVATE uint32_t cc_declaration_bind(cc *c, const cc_declaration_specifiers *ds, cc_declarator *d, qty t, bool file);
/* stmt.c */
CC_PRIVATE uint32_t cc_label_reference(cc *c, uint32_t name, uint32_t loc);
CC_PRIVATE bool cc_library_builtin_is_disabled(const cc_opts *o, const char *name, size_t len);
CC_PRIVATE uint32_t cc_statement_new(cc *c, int op, uint32_t loc, uint32_t a, uint32_t b, uint32_t cc_);
CC_PRIVATE uint32_t cc_asm_statement_parse(cc *c, bool toplevel);
CC_PRIVATE bool cc_token_at_asm(cc *c);
CC_PRIVATE uint32_t cc_compound_statement_parse(cc *c, qty *last);
CC_PRIVATE uint32_t cc_statement_parse(cc *c);
CC_PRIVATE void cc_return_stack_address_check(cc *c, uint32_t e, bool addr);
/* lazy.c */
CC_PRIVATE bool cc_lazy_body_eligible(cc *c, uint32_t s, const cc_declarator *d);
CC_PRIVATE void cc_lazy_body_capture(cc *c, uint32_t s, const cc_declarator *d);
CC_PRIVATE void cc_lazy_body_replayed(const lzfn *L, uint32_t n);
CC_PRIVATE void cc_lazy_body_closure(cc *c);
/* func.c */
CC_PRIVATE void cc_function_definition_parse(cc *c, uint32_t s, const cc_declarator *d);
CC_PRIVATE void cc_translation_unit_parse(cc *c);
CC_PRIVATE uint32_t cc_instrument_functions(cc *c, uint32_t fn, uint32_t body);
CC_PRIVATE void cc_function_object_size_locals(cc *c, uint32_t sym0, uint32_t node0);
CC_PRIVATE void cc_function_body_fold(cc *c, uint32_t sym0, uint32_t node0);
CC_PRIVATE void cc_return_type_check(cc *c, uint32_t s, uint32_t body, uint32_t rbrace, uint32_t lab0, int k);
CC_PRIVATE bool cc_switch_is_enum_covered(cc *c, const node *sw);
/* type-name.c, layout-check.c */
CC_PRIVATE const char *tyname(cc *c, qty t);
CC_PRIVATE void cc_layout_check(cc *c);
CC_PRIVATE const char *cc_type_integer_name(int k);
struct vbuffer;
CC_PRIVATE void cc_type_spell(cc *c, qty t, const char *inner, struct vbuffer *out, bool *ok);

static inline bool cc_type_kind_is_integer(int k) { return (k >= TY_BOOL && k <= TY_UINT128) || k == TY_BITINT || k == TY_UBITINT; }
static inline bool cc_type_kind_is_float(int k) { return k >= TY_BFLOAT16 && k <= TY_FLOAT128; }
/* long double as binary128 (aarch64 Linux; Apple's is double) or x87 extended (x86-64), in
   binary128's layout (include/common/softfloat.h) */
static inline bool cc_type_is_float128(cc *c, qty t) { return (AST_TYPE_KIND(c, t) == TY_LDOUBLE && c->ty[QI(t)].size == 16) || AST_TYPE_KIND(c, t) == TY_FLOAT128; }
/* the fraction bits a wide constant of type t is rounded to: 112, or x87's 63 */
static inline int cc_float_precision_bits(cc *c, qty t) { return c->o->target == PP_LINUX_X86_64 && AST_TYPE_KIND(c, t) == TY_LDOUBLE ? VSOFTFLOAT_X87_80 : 112; }
/* the binary128 bits of a 16-byte long double constant: the two pool slots after its double */
static inline void cc_float_pool_append_float128(cc *c, u128 q) { memcpy(&c->fpool[c->nfpool], &q, 16); c->nfpool += 2; }
static inline u128 cc_float_pool_float128(const cc *c, uint32_t a) { u128 q; memcpy(&q, &c->fpool[a + 1], 16); return q; }

static inline int cc_token_keyword(const ptok *t)
{
	return t->kind == LEX_TOKEN_IDENTIFIER && t->id < KW_END ? (int)cc_keyword_map[t->id] : 0;
}

/* inline at every token consumer: a call to pp_token_next and a test; the rest out of line */
static inline __attribute__((always_inline)) void cc_token_fetch(cc *c, ptok *t)
{
	if (unlikely(c->lz_on))
		cc_token_fetch_lazy(c, t);
	else
		pp_token_next(c->p, t);
	if (unlikely(t->kind == PP_TOKEN_KIND_DIRECTIVE_LINE))
		cc_token_fetch_directive(c, t);
}

static inline ptok *tok(cc *c) { return &c->tk[0]; }

/* clang's nullability qualifiers: no meaning for code generation */
static inline bool cc_token_is_nullability(const cc *c, const ptok *t)
{
	return t->kind == LEX_TOKEN_IDENTIFIER && (t->id == c->id_null[0] || t->id == c->id_null[1] ||
				      t->id == c->id_null[2] || t->id == c->id_null[3]);
}

_Static_assert(offsetof(ptok, kind) == offsetof(ptok, loc) + 4 && offsetof(ptok, flags) == offsetof(ptok, loc) + 5 &&
	       offsetof(ptok, ctr) == offsetof(ptok, loc) + 6 && offsetof(ptok, loc) % 8 == 0, "next(): ptok's loc..ctr");
_Static_assert(offsetof(cc, prevflags) == offsetof(cc, prevloc) + 5, "next(): cc's prev*");

static inline __attribute__((always_inline)) void next(cc *c)
{
	memcpy(&c->prev8, &c->tk[0].loc, 8);
	if (unlikely(c->ntk > 1))
		cc_token_next_shift(c);
	else
		cc_token_fetch(c, &c->tk[0]);
}

static inline bool cc_token_is(cc *c, int kind) { return c->tk[0].kind == kind; }
static inline bool cc_token_is_keyword(cc *c, int kw) { return cc_token_keyword(&c->tk[0]) == kw; }

static inline __attribute__((always_inline)) bool cc_token_accept(cc *c, int kind)
{
	if (c->tk[0].kind != kind)
		return false;
	next(c);
	return true;
}

static inline __attribute__((always_inline)) void cc_token_expect(cc *c, int kind, const char *what)
{
	if (unlikely(c->tk[0].kind != kind))
		cc_token_expect_fail(c, what);
	next(c);
}

/* an opening '(' whose ')' is cc_token_expect_match'ed: its location */
static inline __attribute__((always_inline)) uint32_t cc_token_expect_left_paren(cc *c)
{
	uint32_t l = c->tk[0].loc;

	cc_token_expect(c, LEX_TOKEN_LEFT_PAREN, "'('");
	return l;
}

/* a ')' clang diagnoses at the token, without the note (the builtins' closers) */
static inline __attribute__((always_inline)) void cc_token_expect_here(cc *c, int kind, const char *what)
{
	if (unlikely(c->tk[0].kind != kind))
		cc_token_expect_here_fail(c, what);
	next(c);
}

/* a closing ')' ']' '}' for the opening one at open (clang's "to match this" note) */
static inline __attribute__((always_inline)) void cc_token_expect_match(cc *c, int k, uint32_t open)
{
	if (unlikely(c->tk[0].kind != k))
		cc_recovery_expect_match_fail(c, k, open);
	else
		next(c);
}

/* the same after the expression e: when e is invalid, the skip is silent (clang) */
static inline __attribute__((always_inline)) void cc_token_expect_close(cc *c, int k, uint32_t open, uint32_t e)
{
	if (likely(c->tk[0].kind == k))
		next(c);
	else if (AST_NODE_IS_ERROR(e))
		cc_recovery_skip_close(c, k);
	else
		cc_recovery_expect_match_fail(c, k, open);
}

static inline uint32_t cc_scope_get(uint32_t **dir, uint32_t id)
{
	uint32_t *pg = dir[id >> DIR_BITS];

	return pg ? pg[id & (DIR_PAGE - 1)] : 0;
}

/* an identifier (not a keyword) */
static inline bool cc_symbol_is_typedef_identifier(cc *c, uint32_t id)
{
	uint32_t s = cc_symbol_lookup(c, id);

	return s && c->syms[s].kind == S_TYPEDEF;
}

/* the source range of node n began at token loc b and ends at the previous token */
static inline void cc_node_span_set(cc *c, uint32_t n, uint32_t b)
{
	if (n && !c->span[n].e)
		c->span[n] = (diag_span){ b, c->prevloc };
}

/* the tracked variable s is assigned inside an if/loop/switch nested deeper than its declaration */
static inline bool cc_uninitialized_is_assigned_deeper(cc *c, uint32_t s)
{
	for (uint32_t i = 0; i < c->nuv; i++)
		if (c->uv[i] == s)
			return c->cnd > c->uvc[i];
	return false;
}

/* -Wuninitialized tracks it (clang's isTrackedVar: scalar or vector, not _Atomic) */
static inline bool cc_uninitialized_is_tracked(cc *c, qty t)
{
	t = canon(c, t);
	if (AST_TYPE(c, t)->flags & TF_ATOMIC)
		return false;
	/* (records: clang tracks them in C, not an empty one) */
	return cc_type_is_scalar(c, t) || AST_TYPE_KIND(c, t) == TY_VECTOR || (cc_type_is_record(c, t) && cc_type_complete(c, t) && cc_type_size(c, t) > 0);
}

static inline uint32_t cc_conversion_decay(cc *c, uint32_t n)
{
	qty t = AST_NODE(n)->ty;
	int k = AST_TYPE_KIND(c, t);

	if (unlikely(AST_NODE(n)->op == N_INDEX))
		cc_array_bounds_warn(c, n, false);	/* returns at once for anything else */

	if (likely(k != TY_ARRAY && k != TY_FUNC && !(AST_TYPE(c, t)->flags & TF_ATOMIC)))
		return n;
	return cc_conversion_decay_slow(c, n, t, k);
}

/* the value type of an _Atomic(AST_TYPE) (AST_TYPE, qualifiers kept); anything else unchanged */
static inline qty cc_type_unatomic(cc *c, qty t)
{
	const ctype *x = AST_TYPE(c, t);

	return x->flags & TF_ATOMIC ? QT(x->n, QQ(t)) : t;
}

static inline uint32_t cc_conversion_apply(cc *c, uint32_t n, qty to)
{
	if ((AST_NODE(n)->flags & NF_LVALUE) && (AST_TYPE(c, AST_NODE(n)->ty)->flags & TF_ATOMIC) && cc_atomic_wide_type_is_wide(c, AST_NODE(n)->ty))
		n = cc_atomic_wide_load_build(c, n);
	to = QUNQ(to);
	if (QI(canon(c, AST_NODE(n)->ty)) == QI(canon(c, to)) && !cc_node_bitfield(c, n))
		return n;
	return cc_conversion_apply_slow(c, n, to);
}

static inline uint32_t cc_postfix_parse(cc *c, uint32_t n)
{
	int k = tok(c)->kind;

	if (likely(k != LEX_TOKEN_LEFT_SQUARE && k != LEX_TOKEN_LEFT_PAREN && k != LEX_TOKEN_PERIOD && k != LEX_TOKEN_ARROW && k != LEX_TOKEN_PLUS_PLUS && k != LEX_TOKEN_MINUS_MINUS))
		return n;
	return cc_postfix_operators_parse(c, n);
}

static inline bool cc_token_at_attribute(cc *c)
{
	int kw = cc_token_keyword(tok(c));

	return kw == KW___attribute__ || kw == KW___attribute;
}

/* C23 "[[" starts an attribute specifier */
static inline bool cc_token_at_standard_attribute(cc *c)
{
	return cc_token_is(c, LEX_TOKEN_LEFT_SQUARE) && cc_token_peek(c, 1)->kind == LEX_TOKEN_LEFT_SQUARE;
}

/* most declarators have none: keep the check inline */
static inline void cc_attribute_list_parse(cc *c, attrs *a)
{
	if (cc_token_at_attribute(c) || cc_token_at_standard_attribute(c))
		cc_attribute_list_parse_slow(c, a);
}

/* no attribute at all (padding excluded) */
/* the u32 fields then the 0/1 flag bytes packed .. noinstr (attr.c asserts the layout) */
/* the typedef a declaration records (sym/field tdef, read by -Wformat): the declared type, or the
   one level below it (a function's result, an array's element, a pointer's target) when that is it */
static inline uint32_t cc_typedef_sugar_declared(cc *c, uint32_t td, qty dsty, qty t)
{
	int k;

	if (!td || QUNQ(t) == QUNQ(dsty))
		return td;
	k = AST_TYPE_KIND(c, t);
	if ((k == TY_FUNC || k == TY_ARRAY || k == TY_PTR) && QUNQ(AST_TYPE(c, t)->base) == QUNQ(dsty))
		return td;
	return 0;
}

static inline bool cc_attribute_is_empty(const attrs *a)
{
	const unsigned char *p = (const unsigned char *)a;
	unsigned o = a->prio | a->tlsmodel | a->fmtarg;

	for (size_t i = 0; i <= offsetof(attrs, unavail); i++)
		o |= p[i];
	return !o;
}

#endif
