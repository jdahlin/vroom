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

typedef struct cxx_entity {
	uint32_t name;			/* interned id */
	uint8_t kind;
	uint8_t dependent;		/* a type that depends on template parameters (its members are unknown) */
	cxx_scope *members;		/* namespace or class: its scope; a typedef to a class: that class's scope */
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
	uint32_t implicit_typename;	/* > 0: C++20 P0634 contexts, where a dependent qualified name is a type */		/* a lookup found something that depends on template parameters */	/* the class the last nested-name-specifier named, even if dependent */
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
VPRIV bool cxx_accept_greater(cxx *c);	/* a > ending template arguments: also the first half of >>, >=, >>= */
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
