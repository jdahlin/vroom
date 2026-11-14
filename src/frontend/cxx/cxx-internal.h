/*
 * cxx-internal.h: the C++ front end's shared types (PLAN-cxx.md X3/X4).
 *
 * The proof of concept's test case is nlohmann/json's test suite and nothing
 * else: what is here is what json and the libc++ headers it includes need.
 *
 * Today the front end is a recognizer that looks names up while it parses:
 * C++ cannot be parsed without knowing whether a name is a type, a template
 * or a value (a<b>(c)), so scopes and declarations are real from the start;
 * the AST comes next.  The TU's tokens are read into one array first, so a
 * tentative parse (declaration or expression?) backtracks by resetting an
 * index; the few in-place edits (splitting >> after a template argument
 * list) go through an undo log.
 */
#ifndef VCC_CXX_INTERNAL_H
#define VCC_CXX_INTERNAL_H
#include <pthread.h>
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "../c/cc-internal.h"	/* the shared AST (frontend/ast.h) and the code that builds it */
#include "frontend/cxx/cxx.h"

#ifndef VPRIV
#ifdef VCC_UNITY
#define VPRIV static
#else
#define VPRIV
#endif
#endif

/* token kinds beyond the lexer's: C++'s joined punctuators */
enum {
	CXX_TOKEN_PERIOD_STAR = PP_TOKEN_KIND_DIRECTIVE_LINE + 1,	/* .* */
	CXX_TOKEN_ARROW_STAR,		/* ->* */
	CXX_TOKEN_SPACESHIP,		/* <=> */
	CXX_TOKEN_KIND_COUNT
};

typedef struct cxx_token {
	const char *s;
	uint32_t len, id, loc;
	uint16_t kind;
	uint16_t kw;		/* CXX_KW_* for identifiers that are keywords, else 0 */
} cxx_token;

/* keywords: C++20 and the GNU spellings libc++ and glibc use */
#define CXX_KEYWORDS(X) \
	X(alignas) X(alignof) X(asm) X(auto) X(bool) X(break) X(case) X(catch) X(char) X(char8_t) X(char16_t) X(char32_t) \
	X(class) X(concept) X(const) X(consteval) X(constexpr) X(constinit) X(const_cast) X(continue) X(co_await) \
	X(co_return) X(co_yield) X(decltype) X(default) X(delete) X(do) X(double) X(dynamic_cast) X(else) X(enum) \
	X(explicit) X(export) X(extern) X(false) X(float) X(for) X(friend) X(goto) X(if) X(inline) X(int) X(long) \
	X(mutable) X(namespace) X(new) X(noexcept) X(nullptr) X(operator) X(private) X(protected) X(public) X(register) \
	X(reinterpret_cast) X(requires) X(return) X(short) X(signed) X(sizeof) X(static) X(static_assert) X(static_cast) \
	X(struct) X(switch) X(template) X(this) X(thread_local) X(throw) X(true) X(try) X(typedef) X(typeid) X(typename) \
	X(union) X(unsigned) X(using) X(virtual) X(void) X(volatile) X(wchar_t) X(while) \
	X(__attribute__) X(__attribute) X(__typeof__) X(__typeof) X(__decltype) X(__extension__) X(__restrict) \
	X(__restrict__) X(__inline) X(__inline__) X(__asm__) X(__asm) X(__volatile__) X(__volatile) X(__const) X(__const__) \
	X(__signed) X(__signed__) X(__alignof__) X(__alignof) X(__int128) X(__builtin_va_arg) X(__builtin_offsetof) \
	X(__real__) X(__imag__) X(__label__) X(_Alignas) X(_Alignof) X(_Atomic) X(_Bool) X(_Complex) X(_Static_assert) \
	X(_Thread_local) X(_Noreturn) X(__thread) X(__null) X(__float128) X(__bf16) X(_Float16) X(__fp16) X(__auto_type) \
	X(__complex__) X(__builtin_bit_cast) X(__builtin_addressof) X(__declspec) X(__unaligned) X(__nullptr) \
	X(__builtin_LINE) X(__builtin_FILE) X(__builtin_FUNCTION) X(__builtin_COLUMN) X(__builtin_FILE_NAME) \
	X(__builtin_source_location) X(_Nullable) X(_Nonnull) X(_Null_unspecified) X(__builtin_types_compatible_p) \
	X(__builtin_convertvector) X(__builtin_choose_expr) X(__builtin_shufflevector) X(typeof) X(__is_identifier) X(_BitInt)

enum {
	CXX_KW_NONE,
#define CXX_KW_ENUM(n) CXX_KW_##n,
	CXX_KEYWORDS(CXX_KW_ENUM)
#undef CXX_KW_ENUM
	CXX_KW_COUNT
};

/* what a name denotes, as far as parsing needs to know */
enum {
	CXX_ENTITY_VALUE = 1,		/* variable, function, enumerator, data member */
	CXX_ENTITY_TYPE,		/* class, enum, typedef, alias, type template parameter */
	CXX_ENTITY_NAMESPACE,
	CXX_ENTITY_TEMPLATE,		/* class or alias template, template template parameter */
	CXX_ENTITY_FUNCTION_TEMPLATE,	/* function template (a template-id, then a call) */
	CXX_ENTITY_VARIABLE_TEMPLATE,
	CXX_ENTITY_CONCEPT,
	CXX_ENTITY_DEPENDENT,		/* a member of a dependent type: what typename/template say */
};

typedef struct cxx_scope cxx_scope;
typedef struct cxx_template cxx_template;

/* a template parameter (in its template scope, in order) */
typedef struct cxx_tparam {
	uint8_t kind;		/* 1 type, 2 value, 3 template */
	uint8_t pack;
	uint32_t name;
	uint32_t def_b, def_e;	/* its default argument's tokens (0, 0: none) */
} cxx_tparam;

/* a template argument, as far as it is known: a type or an integer value */
typedef struct cxx_targ {
	const struct cxx_targ *list;	/* kind 6: the argument list the deduced pack is a slice of (not hashed) */
	uint8_t kind;		/* 0 unknown, 1 type, 2 value, 3 a value placeholder (v: its parameter's index), 4 a template
			   (v: its entity), 5 a pack placeholder's expansion (v: index), 6 a deduced pack (t: first, v: count) */
	qty t;
	int64_t v;
} cxx_targ;

/* a type the parser cannot give yet (dependent, a template-id, auto, decltype): qty 0 is void */
#define CXX_TY_UNKNOWN ((qty)0xFFFFFFFFu)

typedef struct cxx_entity {
	uint32_t name;			/* interned id */
	uint8_t kind;
	uint8_t dependent;		/* a type that depends on template parameters (its members are unknown) */
	cxx_scope *members;		/* namespace or class: its scope; a typedef to a class: that class's scope */
	qty ty;				/* a type: its AST type (CXX_TY_UNKNOWN: not known yet) */
	cxx_template *tmpl;		/* a class or alias template: what instantiating it needs (instantiate.c) */
	struct cxx_instance *instance;	/* an instance: its cache entry (completed on demand) */
	uint32_t sym;			/* a variable or function: its AST symbol (0: none yet) */
	int64_t value;			/* a constant (value template argument, constexpr/const integer): its value */
	uint16_t placeholder;		/* a value parameter of a partial specialization being matched: its index + 1 */
	/* a parameter pack bound to its arguments (instantiate.c): an expansion re-parses its pattern once
	   per element, pack_index then the element (ty, value) */
	struct cxx_targ *pack;
	uint32_t npack;
	int32_t pack_index;
	uint8_t is_pack;
	uint16_t pack_placeholder;	/* a partial specialization's pack parameter: its index + 1 */
	uint8_t has_value;
	cxx_scope **specs;		/* class template: its partial and explicit specializations' scopes */
	uint32_t nspecs;
	struct cxx_entity *next;	/* the next entity with the same name in the same scope */
} cxx_entity;

enum { CXX_SCOPE_NAMESPACE = 1, CXX_SCOPE_CLASS, CXX_SCOPE_BLOCK, CXX_SCOPE_TEMPLATE, CXX_SCOPE_FUNCTION_PARAMETERS };

struct cxx_scope {
	uint8_t kind;
	uint8_t dependent;		/* a class template's scope (members of its bases are unknown) */
	uint8_t inline_namespace;
	cxx_scope *parent;
	cxx_entity *owner;		/* the namespace or class */
	/* names declared here: a small open-addressed table of entity chains by name */
	cxx_entity **slots;
	uint32_t nslots, nnames;
	/* namespace: inline namespaces and using-directives (searched with it); class: base class scopes */
	cxx_scope **used;
	uint32_t nused, cused;
	uint8_t has_dependent_base;
	cxx_tparam *tparams;		/* a template scope: its parameters */
	uint32_t ntparams;
};

typedef struct cxx_undo { uint32_t index; cxx_token token; } cxx_undo;

/* a member function body, default argument or initializer parsed once its class is complete */
typedef struct cxx_late { uint32_t begin, end; cxx_scope *scope; uint8_t what; } cxx_late;

typedef struct cxx {
	pp *p;
	const cxx_options *o;
	cc *ast;			/* the TU's AST tables (struct ast inside struct cc), what code generation reads */
	cxx_token *tok;			/* the TU's tokens, then CXX_EOF */
	uint32_t ntok, pos;
	cxx_undo *undo;
	uint32_t nundo, cundo;
	cxx_scope *scope;		/* the innermost */
	cxx_scope *global;
	cxx_late *late;
	uint32_t nlate, clate;
	int tentative;			/* > 0: inside a tentative parse (an error backtracks) */
	jmp_buf *fail;			/* where an error goes: the tentative parse's, or the TU's */
	uint32_t furthest;		/* the furthest token reached (progress, for the report) */
	uint32_t error_pos;
	char error[256];
	uint32_t template_depth;	/* > 0: inside a template declaration (names may be dependent) */
	uint32_t in_template_args;	/* > 0: a > ends the argument list */
	/* the type the last type-specifier named (for typedefs to classes, constructors) */
	cxx_entity *last_type;
	uint8_t last_type_dependent;
	uint32_t class_depth;		/* > 0: inside a class body (member bodies are parsed late) */
	uint32_t explicit_specialization;	/* > 0: after template<> (the declarator names a specialization) */
	cxx_scope *qualifier_hint;
	uint8_t dependent_seen;
	/* types (types.c): the decl-specifiers' type, a declarator's base and result */
	qty ds_type, type_in, type_out;
	qty *fn_params;			/* the last parameter clause's types (types.c) */
	uint32_t fn_nparams, fn_cparams;
	uint8_t fn_variadic, fn_unknown;
	struct cxx_class_build *build;	/* the innermost class being defined (types.c) */
	qty anonymous_class;		/* the type of the last unnamed class defined (an anonymous member's) */
	uint8_t defined_unnamed_class;	/* the last decl-specifiers defined an unnamed class */
	uint32_t layout_attributes;	/* attributes seen that change a layout (aligned, packed, no_unique_address) */
	void *class_info;		/* per AST tag: a laid-out class's data size, POD-ness, emptiness (types.c) */
	uint32_t nclass_info;
	struct cxx_stats {
		uint32_t classes, classes_laid_out, classes_skipped[16];	/* (by CXX_SKIP_*) */
		uint32_t declarations, declarations_typed, symbols;
	} stats;
	/* instantiation (instantiate.c) */
	cxx_targ *targs;		/* the last template argument list's arguments */
	uint32_t ntargs, ctargs;
	void *instances;		/* the cache: (template, arguments) -> instance, a table of pointers (instantiate.c) */
	uint32_t ninstances, cinstances;
	uint32_t instantiating;		/* > 0: re-parsing a template's tokens (function bodies are skipped) */
	cxx_entity *instantiation_target;	/* the class an instantiation's class-specifier defines */
	cxx_entity *instantiation_template;	/* ... and the template it instantiates */
	cxx_scope **tag_scope;		/* per AST tag: the class's scope (a type parameter bound to it: T::member) */
	uint32_t ntag_scope;
	void **tag_instance;		/* per AST tag: an instance not completed yet (instantiate.c) */
	void **tag_info;		/* per AST tag: the instance it is (template, arguments), for matching */
	struct { void *owner; uint32_t index; } *tag_placeholder;	/* per AST tag: a placeholder's partial and index + 1 */
	uint32_t ntag_info, ntag_placeholder;
	uint32_t partial_matches, partial_unknown;
	uint32_t ntag_instance;
	char **tag_spelling;		/* per AST tag: its name as C++ source (the layout dump), NULL: none */
	uint32_t ntag_spelling;
	uint32_t instances_made, instances_failed;
	uint8_t trace;			/* VCC_CXX_TRACE: each instantiation to stderr */
	int layout_fd;			/* VCC_CXX_LAYOUT: static_asserts of the layouts, for clang (-1: off) */
	char name_path[512];		/* the qualified name of the scope being parsed (for the layout dump) */
	uint32_t name_path_len;
	uint8_t name_path_valid;	/* every enclosing scope is nameable (no anonymous namespace, no function) */
	uint32_t implicit_typename;	/* > 0: C++20 P0634 contexts, where a dependent qualified name is a type */
	struct cxx_entity *packs_seen[8];	/* the packs lookups found since the last reset (an expansion's) */
	uint32_t npacks_seen;
	uint8_t packs_overflow;
	uint8_t in_template_argument;	/* parsing a template argument's type-id: a trailing ... is an expansion */		/* a lookup found something that depends on template parameters */	/* the class the last nested-name-specifier named, even if dependent */
} cxx;

/* tokens (token.c) */
VPRIV void cxx_tokens_read(cxx *c);
VPRIV const char *cxx_token_kind_spelling(int kind);
static inline cxx_token *cxx_peek(cxx *c, uint32_t ahead) { uint32_t i = c->pos + ahead; return &c->tok[i < c->ntok ? i : c->ntok]; }
static inline int cxx_kind(cxx *c) { return cxx_peek(c, 0)->kind; }
static inline int cxx_kw(cxx *c) { return cxx_peek(c, 0)->kw; }
static inline bool cxx_is(cxx *c, int kind) { return cxx_peek(c, 0)->kind == kind; }
static inline bool cxx_is_kw(cxx *c, int kw) { return cxx_peek(c, 0)->kw == kw; }
static inline void cxx_advance(cxx *c) { if (c->pos < c->ntok) c->pos++; if (c->pos > c->furthest) c->furthest = c->pos; }
static inline bool cxx_accept(cxx *c, int kind) { if (cxx_is(c, kind)) { cxx_advance(c); return true; } return false; }
static inline bool cxx_accept_kw(cxx *c, int kw) { if (cxx_is_kw(c, kw)) { cxx_advance(c); return true; } return false; }
VPRIV __attribute__((noreturn, format(printf, 2, 3))) void cxx_fail(cxx *c, const char *fmt, ...);
VPRIV void cxx_expect(cxx *c, int kind);
VPRIV void cxx_expect_kw(cxx *c, int kw);
VPRIV bool cxx_accept_greater(cxx *c);	/* a > ending template arguments */
VPRIV int cxx_operator_at(cxx *c, uint32_t *ntok);	/* the operator here, > pieces joined: kind, tokens */
VPRIV void cxx_skip_balanced(cxx *c);	/* from an opening ( [ { to after its closing one */
VPRIV uint32_t cxx_keyword_of(uint32_t id);

/* tentative parsing: CXX_TRY(c) { ... } returns true if the block parsed without error */
typedef struct cxx_mark { uint32_t pos, nundo; jmp_buf *fail; cxx_scope *scope; uint32_t template_depth, in_template_args; } cxx_mark;
VPRIV void cxx_mark_set(cxx *c, cxx_mark *m);
VPRIV void cxx_mark_rewind(cxx *c, const cxx_mark *m);
VPRIV void cxx_rewind(cxx *c, uint32_t pos, uint32_t nundo);

/* names (names.c) */
VPRIV cxx_scope *cxx_scope_new(cxx *c, int kind, cxx_scope *parent, cxx_entity *owner);
VPRIV void cxx_scope_push(cxx *c, cxx_scope *s);
VPRIV void cxx_scope_pop(cxx *c);
VPRIV cxx_entity *cxx_declare(cxx *c, cxx_scope *s, uint32_t name, int kind);
VPRIV void cxx_scope_use(cxx *c, cxx_scope *s, cxx_scope *used);
VPRIV cxx_entity *cxx_lookup_in(cxx *c, cxx_scope *s, uint32_t name, int depth);
VPRIV cxx_entity *cxx_lookup(cxx *c, uint32_t name);
VPRIV cxx_entity *cxx_lookup_local(cxx_scope *s, uint32_t name);
VPRIV cxx_scope *cxx_enclosing_namespace(cxx *c);
VPRIV cxx_scope *cxx_declaration_scope(cxx *c);	/* where a declaration lands (template scopes skipped) */
VPRIV cxx_entity *cxx_entity_dependent(void);
VPRIV int cxx_builtin_kind(uint32_t id);	/* CXX_BUILTIN_*: clang's type-trait and type-transform names */
enum { CXX_BUILTIN_NONE, CXX_BUILTIN_TRAIT, CXX_BUILTIN_TYPE_TRANSFORM, CXX_BUILTIN_TEMPLATE };

/* types (types.c) */
enum { CXX_SKIP_TEMPLATE, CXX_SKIP_BASES, CXX_SKIP_VIRTUAL, CXX_SKIP_UNKNOWN_MEMBER, CXX_SKIP_BITFIELD, CXX_SKIP_SPECIALIZATION,
       CXX_SKIP_ATTRIBUTE, CXX_SKIP_EMPTY_BASE, CXX_SKIP_COUNT };
typedef struct cxx_spec {
	uint8_t v_void, v_bool, v_char, v_char8, v_char16, v_char32, v_wchar, v_int, v_float, v_double;
	uint8_t v_short, v_long, v_signed, v_unsigned, v_int128, v_float128, v_float16, v_bf16, v_unknown, quals;
	qty named;			/* a type name's type (0 with v_named unset) */
	uint8_t v_named;
} cxx_spec;
VPRIV qty cxx_spec_type(cxx *c, const cxx_spec *s);
VPRIV qty cxx_type_pointer(cxx *c, qty base, int quals);
VPRIV qty cxx_type_reference(cxx *c, qty base, bool rvalue);
VPRIV qty cxx_type_array(cxx *c, qty elem, int64_t n);	/* n < 0: unknown bound */
VPRIV qty cxx_type_function(cxx *c, qty ret, const qty *params, uint32_t n, bool variadic);
VPRIV qty cxx_type_parameter_adjust(cxx *c, qty t);
VPRIV void cxx_class_begin(cxx *c, cxx_entity *e, uint32_t name, uint32_t loc, bool is_union, int skip, bool is_class);
VPRIV void cxx_class_skip(cxx *c, int reason);
VPRIV void cxx_class_field(cxx *c, uint32_t name, qty t, uint32_t loc, int bitfield_width);
VPRIV void cxx_class_end(cxx *c);
VPRIV void cxx_class_base(cxx *c, qty t, bool is_virtual);
VPRIV void cxx_class_access(cxx *c, int non_public);
VPRIV void cxx_class_member_traits(cxx *c, int what);	/* CXX_MEMBER_*: what makes a class non-POD */
enum { CXX_MEMBER_USER_SPECIAL = 1, CXX_MEMBER_INITIALIZER, CXX_MEMBER_VIRTUAL };
VPRIV void cxx_enum_type(cxx *c, cxx_entity *e, uint32_t name, uint32_t loc, qty underlying);
VPRIV uint32_t cxx_symbol(cxx *c, int kind, uint32_t name, qty t, uint32_t loc);
VPRIV void cxx_name_path_push(cxx *c, uint32_t name, uint32_t *saved_len, uint8_t *saved_valid);
VPRIV void cxx_name_path_pop(cxx *c, uint32_t saved_len, uint8_t saved_valid);

/* instantiation (instantiate.c) */
VPRIV cxx_template *cxx_template_of(cxx_entity *e);	/* (allocates) */
VPRIV void cxx_template_define(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t pos, uint32_t end, bool alias);
VPRIV void cxx_template_partial(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t key_pos);
VPRIV void cxx_template_declared(cxx *c, cxx_entity *e, cxx_scope *params);
VPRIV void cxx_tparam_add(cxx *c, int kind, uint32_t name, bool pack, uint32_t def_b, uint32_t def_e);
VPRIV cxx_entity *cxx_instantiate(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);
VPRIV void cxx_instance_complete(cxx *c, cxx_entity *inst);
VPRIV void cxx_type_require_complete(cxx *c, qty t);
VPRIV cxx_scope *cxx_type_class_scope(cxx *c, qty t);
VPRIV void cxx_tag_instance_set(cxx *c, uint32_t tag, void *x);
VPRIV void cxx_tag_info_set(cxx *c, uint32_t tag, void *x);
VPRIV void cxx_pack_element(cxx *c, cxx_entity *e, int32_t i);	/* i < 0: back to the whole pack */
VPRIV void cxx_instance_register(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst);
VPRIV bool cxx_type_spell(cxx *c, qty t, char *buf, size_t cap, size_t *len);
VPRIV void cxx_tag_spelling_set(cxx *c, uint32_t tag, const char *s, size_t n);
VPRIV void cxx_tag_scope_set(cxx *c, uint32_t tag, cxx_scope *s);
VPRIV void cxx_class_specifier_for_instance(cxx *c);
VPRIV bool cxx_template_id_spell(cxx *c, cxx_entity *te, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);

/* parsing (declaration.c, expression.c, statement.c) */
VPRIV void cxx_translation_unit_parse(cxx *c);
VPRIV void cxx_declaration_parse(cxx *c, int context);
VPRIV bool cxx_starts_declaration(cxx *c);
VPRIV void cxx_type_id_parse(cxx *c);
VPRIV void cxx_type_specifiers_parse(cxx *c);
VPRIV void cxx_new_type_id_parse(cxx *c);
VPRIV bool cxx_try_type_id(cxx *c);
VPRIV void cxx_expression_parse(cxx *c);
VPRIV void cxx_assignment_expression_parse(cxx *c);
VPRIV void cxx_conditional_expression_parse(cxx *c);
VPRIV void cxx_constant_expression_parse(cxx *c);
VPRIV void cxx_initializer_parse(cxx *c);
VPRIV void cxx_braced_init_list_parse(cxx *c);
VPRIV void cxx_compound_statement_parse(cxx *c);
VPRIV void cxx_statement_parse(cxx *c);
VPRIV void cxx_template_arguments_parse(cxx *c);
VPRIV void cxx_attributes_skip(cxx *c);
VPRIV void cxx_requires_clause_parse(cxx *c);
VPRIV void cxx_late_parse_all(cxx *c, uint32_t from);
VPRIV cxx_entity *cxx_nested_name_parse(cxx *c, cxx_scope **qualifier, bool *dependent);

enum { CXX_CONTEXT_NAMESPACE = 1, CXX_CONTEXT_CLASS, CXX_CONTEXT_BLOCK, CXX_CONTEXT_TEMPLATE_PARAMETER };
#endif
