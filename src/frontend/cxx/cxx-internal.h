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

#define AST_NODE_AT(a, i) (&(a)->nodes[i])	/* the AST node i */

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
	uint32_t type_b;	/* a value parameter: its type's tokens start (0: not recorded) */
	uint32_t constraint_b;	/* a constrained type parameter (template<C T>): its concept's tokens start (0: none) */
} cxx_tparam;

/* a template argument, as far as it is known: a type or an integer value */
/* a deduction guide Name(P1, ..., Pn) -> Name<T...>; each Pi is Tk, const Tk & or Tk && of the guide's own
   type parameters; the result names them in some order (pair(T1, T2) -> pair<T1, T2>) */
typedef struct cxx_guide {
	uint8_t np, nres;
	uint8_t pidx[8], mode[8];	/* parameter i: its template parameter, and 0 by value (decays), 1 const T &, 2 T && */
	uint8_t res[8];			/* the result's arguments: template parameter indexes */
} cxx_guide;

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
	const struct cxx_targ *targs;	/* a function template's instance: its template arguments (the mangled name) */
	uint32_t ntargs;
	uint8_t is_instance;
	struct cxx_scope *inst_params;	/* ... the template's parameters (their value types mangle the arguments) */
	struct cxx_ftemplate *inst_ftmpl;
	uint32_t inst_pack_at;		/* ... a pack's arguments from here (UINT32_MAX: none; mangled J...E) */	/* ... its template (its declared signature mangles it) */
	uint32_t abi_tag;		/* a function declared with __attribute__((abi_tag("x"))): x, interned (0: none) */
	uint8_t no_explicit_inst;	/* __attribute__((exclude_from_explicit_instantiation)): an extern template does not cover it */
	uint32_t sym1;			/* a constructor or destructor: the complete-object function's symbol (C1, D1; sym is C2, D2) */
	int64_t value;			/* a constant (value template argument, constexpr/const integer): its value */
	struct cxx_entity *bound_template;	/* a template template parameter bound to a template: that template (its identity) */
	struct cxx_ftemplate *ftmpl;
	struct cxx_late *late;		/* a member function of an instance: its body, parsed when a call uses it */
	uint8_t late_done;
	cxx_scope *member_of;		/* a member function: its class's scope (an instance's: its out-of-class definition is found by it) */
	uint8_t fn_const;		/* a member function declared const */
	uint8_t fn_noexcept;		/* ... declared noexcept: 1, noexcept(false) or none: 0, a condition not known: 2 */
	struct cxx_scope *home;		/* a function with a symbol: the namespace it is declared in */
	struct cxx_scope *field_of;	/* a non-static data member: its class */
	uint8_t fn_static;		/* a static member function */
	uint8_t mangled_var;
	uint8_t fn_ooc_default;		/* X::~X() = default; outside its class (not inline) */		/* a variable: its symbol named (cxx_gen_variable_name) */
	uint8_t fn_virtual, fn_pure;	/* a virtual member function (declared so, or it overrides one); = 0 */
	uint32_t vslot;			/* ... its index in the class's vtable + 1 (a destructor: the complete one's, the deleting one's follows) */
	uint32_t sym0;			/* a virtual destructor: the deleting destructor's symbol (D0) */
	uint8_t fn_internal;		/* a function declared static at namespace scope: internal linkage */
	uint8_t fn_inline;		/* a function defined in its class or declared inline: emitted weak */
	uint8_t c_linkage, global_ns, mangled;	/* a function: declared extern "C"; in the global namespace; its symbol has its mangled name */
	uint8_t fn_explicit;		/* a constructor declared explicit (2: explicit(expr), not evaluated) */
	uint8_t fn_implicit;		/* a destructor vcc made for a class that declares none (its members and bases need one) */
	uint8_t gen_tried;		/* a defaulted or implicit special member: its body was asked for */
	uint8_t fn_deleted, fn_defaulted;	/* ... = delete, = default */	/* a function template: its signature, for deduction (instantiate.c) */
	uint16_t ndefaults;		/* a function: how many trailing parameters have default arguments */
	uint32_t *defs;			/* ... their tokens (begin, end pairs), parsed again where a call leaves them out; NULL: not recorded */
	cxx_scope *def_scope;		/* ... and the scope they were declared in */
	uint32_t nsdmi_pos;		/* a data member's default initializer: the position of its = or { (0: none recorded) */
	cxx_scope *nsdmi_scope;		/* ... the class scope it is parsed in */
	uint8_t has_nsdmi;
	uint8_t is_mutable;
	uint8_t name_guess;		/* a function whose symbol name is vcc's own (not clang's): called only when vcc defines it */		/* a data member declared mutable (not const in a const object) */		/* ... it has one (recorded or not: a template's pattern is not) */
	uint16_t placeholder;		/* a value parameter of a partial specialization being matched: its index + 1 */
	/* a parameter pack bound to its arguments (instantiate.c): an expansion re-parses its pattern once
	   per element, pack_index then the element (ty, value) */
	struct cxx_targ *pack;
	uint32_t npack;
	uint32_t *pack_syms;		/* a function parameter pack being built: each element's parameter symbol */
	int32_t pack_index;
	uint8_t is_pack;
	uint16_t pack_placeholder;	/* a partial specialization's pack parameter: its index + 1 */
	uint8_t has_value;
	cxx_scope **specs;		/* class template: its partial and explicit specializations' scopes */
	struct cxx_guide *guides;	/* class template: its deduction guides of the simple form vcc deduces from (cxx_ctad) */
	uint32_t nguides;
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
	struct cxx_scope *friends;	/* a class: the functions it declares friend (found through an argument of the class) */
	uint8_t is_final;		/* a class declared final */
	uint32_t ti_sym;		/* the class's type info object (_ZTI...), once named */
	uint8_t ti_listed;
	uint8_t is_lambda;		/* a closure type (its name is the TU's own: $_N) */
	/* a class with virtual functions: its vtable's slots (each one's final overrider), the virtual functions it
	   declares itself in order (the key function is the first that is neither pure nor inline) */
	cxx_entity **vfuncs, **vown;
	uint32_t nvfuncs, cvfuncs, nvown, cvown;
	uint8_t vt_init, vt_bad, vt_listed;	/* vt_bad: more than one base with a vtable (thunks: not built yet) */
	uint32_t vt_sym;		/* the vtable's AST symbol (_ZTV...) */
	qty *conv_types;		/* ... the types its conversion functions convert to */
	uint32_t nconv;
	uint8_t conv_unknown;		/* ... one of them to a type not recorded (a template's, a pointer) */
	uint8_t has_conversion;		/* a class: it declares a conversion function (operator T) */
	qty *field_types;		/* a class: its non-static data members' types, for its implicit special members */
	uint32_t nfield_types;
	cxx_tparam *tparams;		/* a template scope: its parameters */
	uint32_t ntparams;
	struct cxx_entity *fn;		/* a function body's block: the function (its local classes mangle Z <it> E <name>) */
};

typedef struct cxx_undo { uint32_t index; cxx_token token; } cxx_undo;

/* a member function body, default argument or initializer parsed once its class is complete */
/* a function parameter: its name (0: unnamed) and type, declared in the body's scope */
typedef struct cxx_param { uint32_t name; qty ty; } cxx_param;

enum { CXX_WHY_DEPENDENT, CXX_WHY_NONE_VIABLE, CXX_WHY_AMBIGUOUS, CXX_WHY_NO_FTMPL, CXX_WHY_PACKS, CXX_WHY_SIG, CXX_WHY_ARG,
       CXX_WHY_UNBOUND, CXX_WHY_CLASS_MATCH, CXX_WHY_RETURN, CXX_WHY_NO_SET, CXX_WHY_N };

/* code generation: a lambda's captures, found as its body names the enclosing functions' variables */
typedef struct cxx_gen_lambda {
	uint32_t floor;		/* symbols made before this one belong to the enclosing functions */
	uint8_t def;		/* the capture-default: 0 none, 1 [&], 2 [=] */
	uint8_t ex_this, bad;
	uint8_t is_try;		/* a try block's body, outlined: no return, break or continue out of it yet */
	uint32_t nex;
	struct { uint32_t name; uint8_t by_ref; } ex[16];	/* the captures the list names */
	uint32_t n;
	struct {
		uint32_t sym;			/* the captured variable's symbol (0: this) */
		const struct cxx_entity *e;
		uint8_t by_ref, is_this;
		uint32_t local;			/* in operator(): a pointer to the captured object (this: its value) */
		qty fty;			/* the closure's member */
	} cap[32];
} cxx_gen_lambda;

typedef struct cxx_late { uint32_t begin, end; cxx_scope *scope; uint8_t what; cxx_param *params; uint32_t nparams, template_depth; struct cxx_entity *fn;
	uint32_t pack_name, pack_at, pack_n; qty *pack_types; } cxx_late;	/* pack_*: a class pack expanded in the parameters (As... a; pack_n UINT32_MAX: none) */

/* the most arguments (or braced-list elements) a call or list keeps typed: more is not built */
#define CXX_MAX_ARGS 256

typedef struct cxx {
	pp *p;
	const cxx_options *o;
	cc *ast;			/* the TU's AST tables (struct ast inside struct cc), what code generation reads */
	cxx_token *tok;			/* the TU's tokens, then CXX_EOF */
	uint32_t ntok, pos;
	uint32_t tok_cap;		/* tok's capacity (a read resumed after #pragma vcc_fork appends) */
	uint8_t fork_pending;		/* the read stopped at #pragma vcc_fork */
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
	uint32_t *fn_pnames;		/* ... and names */
	qty *fn_pdecl;			/* ... and the parameters' own types in the body (a by-value const stays const) */
	cxx_param *decl_params;		/* the last declarator's function parameters (its body's names) */
	uint32_t ndecl_params, cdecl_params;
	uint32_t decl_ndefaults;	/* ... and how many of them have default arguments */
	uint32_t fn_nparams, fn_cparams;
	uint8_t fn_variadic, fn_unknown;
	uint32_t fn_ndefaults;
	uint32_t fn_defs[16], fn_ndefs;	/* the last parameter list's default arguments: begin, end token pairs (8 at most) */
	uint8_t fn_has_trailing, decl_has_trailing;	/* the last function tail / declarator wrote -> type (known or not) */
	qty fn_trailing;		/* the last function tail's trailing return type (CXX_TY_UNKNOWN: none) */
	uint8_t decl_pack;		/* the last declarator was a pack's (Ts&&... ts) */
	uint32_t fn_pack, decl_pack_param;	/* the last parameter clause's / declarator's pack parameter (its index, UINT32_MAX: none) */
	qty *sig_pack_types;		/* a signature parsed with a body: its parameter pack's element types (sig_pack_n of them; UINT32_MAX: none) */
	uint32_t sig_pack_n;
	uint8_t decl_variadic, sig_is_function;	/* the last declarator's function is variadic; the last signature parsed was a function's */
	uint8_t fn_const, decl_const;	/* the last function tail had const; the last declarator's function did */
	int64_t evalue;			/* the expression's value, when econst (an integer constant expression) */
	uint8_t econst;
	qty btypes[2];			/* the last builtin call's type arguments (nbtypes: 3 if it had other arguments) */
	uint32_t nbtypes;
	uint8_t this_const;		/* the member function being parsed is const */
	uint8_t call_obj;		/* the callee is a member of an object: 1 not const, 2 const (0: not known) */
	struct cxx_class_build *build;	/* the innermost class being defined (types.c) */
	qty anonymous_class;		/* the type of the last unnamed class defined (an anonymous member's) */
	uint8_t defined_unnamed_class;	/* the last decl-specifiers defined an unnamed class */
	uint32_t layout_attributes;	/* attributes seen that change a layout (aligned, packed, no_unique_address) */
	void *class_info;		/* per AST tag: a laid-out class's data size, POD-ness, emptiness (types.c) */
	uint32_t nclass_info;
	struct cxx_stats {
		uint32_t classes, classes_laid_out, classes_skipped[16];	/* (by CXX_SKIP_*) */
		uint32_t declarations, declarations_typed, symbols, expressions, expressions_typed;
		uint32_t asserts_true, asserts_false, asserts_unknown;	/* static_asserts outside templates */
		uint32_t calls, calls_typed, why[12];	/* calls outside templates; why a call has no type (CXX_WHY_*) */
		uint32_t ooc_bodies, ooc_failed, ooc_ambiguous;	/* class template members defined outside the class, instantiated */
		uint32_t functions, functions_generated;	/* function definitions at namespace scope; with code */
		uint32_t bodies, bodies_failed, bodies_cached, body_depth_max, member_bodies, member_bodies_failed;	/* function template bodies instantiated */
	} stats;
	/* instantiation (instantiate.c) */
	cxx_targ *targs;		/* the last template argument list's arguments */
	uint32_t ntargs, ctargs;
	void *instances;		/* the cache: (template, arguments) -> instance, a table of pointers (instantiate.c) */
	uint32_t ninstances, cinstances;
	uint32_t instantiating;		/* > 0: re-parsing a template's tokens (function bodies are skipped) */
	cxx_entity *instantiation_target;	/* the class an instantiation's class-specifier defines */
	cxx_entity *instantiation_template;	/* ... and the template it instantiates */
	cxx_scope **tag_home;		/* per AST tag of an enumeration: the scope it is declared in (its mangled name) */
	uint32_t ntag_home;
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
	int mangle_fd;			/* VCC_CXX_MANGLE: the mangled names of the functions defined (-1: off) */
	int layout_fd;			/* VCC_CXX_LAYOUT: static_asserts of the layouts, for clang (-1: off) */
	char name_path[512];		/* the qualified name of the scope being parsed (for the layout dump) */
	uint32_t name_path_len;
	uint8_t name_path_valid;	/* every enclosing scope is nameable (no anonymous namespace, no function) */
	uint32_t implicit_typename;	/* > 0: C++20 P0634 contexts, where a dependent qualified name is a type */
	struct cxx_entity *packs_seen[8];	/* the packs lookups found since the last reset (an expansion's) */
	uint32_t npacks_seen;
	uint8_t packs_overflow;
	/* expressions' types (typing.c) */
	uint32_t sb_names[8], sb_locs[8], sb_n, sb_var, sb_serial;	/* a structured binding being declared: its names, its variable */
	qty memptr_ty;		/* T C::*: one opaque (incomplete) type for every pointer to member, not built yet */
	qty etype;			/* the last expression's type (CXX_TY_UNKNOWN: not known) */
	uint8_t elval;			/* ... an lvalue */
	cxx_entity *ecallee;		/* a name that is a set of functions: for the call that follows */
	qty *cargs;			/* the last call's argument types */
	uint8_t *cargs_lval;		/* ... and which are lvalues */
	uint32_t ncargs, ccargs;
	qty edecl;			/* the expression's type as cxx_et got it (a call's reference type), for decltype */
	uint8_t edecl_name;		/* ... of an unparenthesized name or member: decltype is its declared type */
	uint8_t einvalid_unsure;	/* argument-dependent lookup found candidates it could not decide between */
	uint8_t ename_missing;		/* ... and ordinary lookup found nothing by that name */
	uint8_t eunsure;		/* a known type was given to something whose operand vcc could not type ((void)x, f(x)): validity not known */
	uint8_t einvalid;		/* the expression is ill-formed for sure (no function fits, no such member): a requirement fails */
	int8_t req_value;		/* the last requires-expression: 1 true, 0 false, -1 not known */
	int8_t sig_requires;		/* the last signature's trailing requires-clause: 1, 0, -1 not known (1: none) */
	uint32_t decl_less;		/* the last declarator-id's template arguments name<...> (its '<', 0: none) */
	uint32_t ftmpl_req;		/* a template declaration's requires-clause (its token, 0: none) */
	uint32_t unevaluated;		/* > 0: in an unevaluated operand or a discarded statement (calls instantiate no body) */
	uint8_t sig_fake;		/* the last signature's function type stands for an unknown parameter list (only its return type is right) */
	uint8_t sig_auto;		/* the last signature parsed returns auto with no trailing type */
	uint32_t ename;			/* the expression is an unqualified name (its id): a call looks it up by its arguments too */
	qty bound_sig;			/* the last deduced template's function type with its arguments bound (or unknown) */
	uint32_t lambda_node;		/* ... code generation: the closure object's node (0: not built) */
	uint32_t lambda_serial;		/* closure types so far (their names: $_N) */
	struct cxx_gen_lambda *gen_lambda;	/* the lambda whose operator() is being built (NULL: an ordinary function) */
	qty lambda_type;		/* the last lambda's closure type (CXX_TY_UNKNOWN: not built) */
	uint8_t ret_seen;		/* ... and a return with a value was seen (its type perhaps not known) */
	uint8_t ret_capture, ret_lval;	/* the body being parsed returns auto: its first return's type is wanted */
	qty ret_type;
	uint32_t range_name;		/* a range-for's declaration: its name, type (auto: a pattern over the placeholder) */
	qty range_type;
	uint8_t range_auto;
	uint8_t ds_auto, ds_quals;	/* the last decl-specifiers were auto (with these cv-qualifiers) */
	uint8_t vt_making;
	qty gen_base_type;		/* code generation: the base class whose subobject is being constructed or destroyed (its C2/D2) */		/* code generation: implicit destructors being made for vtables (a bound on recursion) */
	qty nullptr_ty;			/* std::nullptr_t, the type of nullptr (0: not made yet) */
	qty auto_ty;			/* auto's placeholder type (0: not made yet) */
	struct cxx_partial *auto_owner;
	uint8_t member_missing;		/* typename X::y named no member of a complete class X (deduction: not viable) */
	cxx_entity *call_picked;	/* ... and which */
	uint8_t carg_const;		/* the argument list was one integer constant expression */
	int64_t carg_value;		/* ... its value */
	qty constructing;		/* the class whose constructors are being tried for a conversion (its copy constructor does not take a converted argument) */
	uint8_t no_literal, rank_no_literal;	/* the next call's arguments are types, not expressions (a trait's): an integer is no null pointer constant; while ranking */
	uint32_t einvalid_at;		/* where einvalid was last set (VCC_CXX_SIG) */
	uint8_t op_lv_set, op_lv[17];	/* the next operator call's operands: lvalues or not (the object first) */
	/* code generation (codegen.c): the AST of the function body being parsed */
	uint32_t c_linkage;		/* > 0: inside extern "C" */
	uint32_t gen_level;		/* c->instantiating where the body being built started (deeper: another parse, no nodes) */
	void *gen_stack;		/* the bodies being built around this one (one instantiated inside another) */
	uint32_t gen_stack_depth;
	uint8_t gen, gen_failed;	/* building it; something in it is not built yet (the function stays undefined) */
	uint32_t enode;			/* the expression's AST node (0: not built) */
	uint32_t snode, sdone, sdepth;	/* the statement's node; the statement depth that set it; the depth now */
	void *gen_block;		/* the block being built: its statements (a cc_id_list) */
	uint32_t gen_psyms, gen_npsyms;	/* the function's parameter symbols (a list in the AST) */
	qty gen_ret;			/* its return type */
	uint32_t gen_fn;		/* (the AST's current function, restored after) */
	char **gen_names;		/* the mangled names of the functions defined so far, a hash set (a second definition by the same name gets no code) */
	uint32_t ngen_names, cgen_names;
	/* a constructor's initializers so far: the members (names) and bases (types) they construct */
	uint32_t gen_init_names[32], ngen_init_names;
	qty gen_init_bases[16];
	uint32_t ngen_init_bases;
	uint8_t gen_delegates, gen_returns, gen_why;
	uint8_t cond_decl;		/* parsing an if's or a switch's condition: a declaration there is a real one (code generation) */
	qty conv_last;			/* the conversion-function-id just parsed: the type it converts to (the function's result) */
	uint32_t gen_sret;		/* the function returns a class by address: the parameter that points to the result */
	uint32_t gen_notemp;		/* inside a loop's condition or step, or a conditionally evaluated operand: no temporaries with destructors */
	uint32_t gen_block_temps;	/* such temporaries declared in the block being built (a case label after one: not built) */
	struct cxx_prv *prvs;		/* class prvalues: temporaries whose construction can still be aimed at another object */
	uint32_t nprvs, cprvs;
	uint8_t gen_vmode;		/* the next call of a virtual function: 0 through the vtable, 1 direct (a qualified name), 2 not known */
	cxx_scope **vt_list;		/* the classes whose vtable this TU names */
	uint32_t nvt_list, cvt_list;
	uint32_t gen_delete_sym, gen_pure_sym;
	/* function symbols by assembler name: two entities for one function (found through different declarations) share the symbol */
	struct { const char *name; uint32_t sym; } *sym_names;
	struct cxx_extern_member { struct cxx_entity *of; cxx_targ args[4]; uint32_t nargs, name, nparams; } *externs;	/* extern template R X<args>::f(...): defined elsewhere */
	uint32_t nexterns;
	struct { uint32_t sym, sym1; char *why; } *missing;	/* VCC_CXX_MISSING: functions whose bodies were not built */
	uint32_t nmissing, cmissing;
	uint32_t nsym_names, csym_names;
	cxx_scope **ti_list;		/* the classes whose type info this TU names (defined at its end where it belongs here) */
	uint32_t nti_list, cti_list;
	uint32_t gen_eh_syms[8];	/* the exception runtime's functions, once declared */
	cxx_entity *ginit_fn;		/* a namespace-scope variable's initializer is being built: the function that runs it at start-up */
	void *ginit_list;		/* ... and that function's statements */
	uint32_t ginit_serial;
	uint32_t gen_atexit_sym, gen_dso_sym;
	uint32_t gen_try_serial;	/* try blocks so far (their trampolines' names) */
	uint8_t gen_eh_pers;		/* the personality routine's cell was emitted */	/* it delegates to another constructor; the body has a return statement */
	void *gen_pre;			/* a constructor's member initializers: statements before its body (a cc_id_list) */
	struct cxx_entity *ctor_ilist;	/* ... for a braced list: its std::initializer_list constructor (NULL: none) */
	qty ctor_ilist_types[CXX_MAX_ARGS];	/* ... and the list's elements' types, as sema saw them (nullptr: nullptr_t, its node a void *) */
	uint8_t ctor_ilist_lval[CXX_MAX_ARGS];
	uint32_t ctor_ilist_brace[CXX_MAX_ARGS];	/* ... an element that is a braced list: its '{' (carg_brace) */
	uint32_t ctor_ilist_n;
	struct cxx_entity *ctor_picked;	/* the constructor the last cxx_ctor_use chose (NULL: none declared fits, or none is needed) */
	uint8_t ctor_found;		/* ... it chose one; ctor_none: the class declares no constructor */
	uint8_t ctor_none;
	struct cxx_entity *gen_instance;	/* the function template instance whose signature and body are parsed next (NULL: none) */
	struct cxx_entity *call_instance;	/* ... the one the last body instantiation was for */
	struct cxx_entity *ftmpl_te;	/* the function template being deduced (its entity) */
	qty bfirst;			/* ... its first expression argument's type */
	uint32_t bnodes[4], nbnodes;	/* the last builtin call's expression arguments' nodes (UINT32_MAX: a type among them, or too many) */
	uint32_t gen_builtin_syms[128];	/* the library functions the builtins call, once declared */
	uint32_t gen_this;		/* a member function's body: the symbol of this (0: none) */
	uint32_t eobj_node;		/* a member function named through an object: the object's node */
	uint8_t eobj_arrow;		/* ... through a pointer (p->f) */
	uint32_t carg_nodes[CXX_MAX_ARGS];	/* the last argument list's nodes */
	uint32_t carg_zero;		/* ... which arguments are integer constants 0 (a null pointer constant; bit i: argument i) */
	uint32_t carg_brace[CXX_MAX_ARGS];
	const uint32_t *gen_agg_brace;	/* the next aggregate initialization: its elements that are braced lists (their '{'; NULL: none known) */
	uint32_t gen_call_brace[CXX_MAX_ARGS];	/* the next cxx_gen_call's arguments that are braced lists (their '{'; carg_brace saved at the call) */	/* ... an argument that is a braced list itself: its '{' (its node 0; 0: not one) */
	uint32_t new_count;		/* a new-type-id: its array bound's node (new_array: it has one) */
	uint8_t new_array;
	uint32_t gen_pack_from, gen_pack_n;	/* cxx_gen_function_begin: these parameters are a pack's elements (gen_pack_entity's) */
	struct cxx_entity *gen_pack_entity;
	struct cxx_entity *ooc_target;	/* an instance's member whose out-of-class definition is being parsed */
	struct cxx_entity *fn_set_seen;	/* the last name of functions parsed (an overload set a cast picks from) */
	uint8_t fn_noexcept, decl_noexcept;	/* the function declarator's noexcept (cxx_noexcept_parse) */
	int8_t constructs_nothrow;	/* cxx_class_constructs: the constructor it found is noexcept (1, 0; -1 not known) */
	uint8_t mangle_guess;		/* cxx_mangle_function: the name is vcc's own (a template's declared signature not known) */
	uint32_t decl_start;		/* the declaration being parsed: its decl-specifiers' first token */
	uint8_t bconst;			/* a builtin call's first argument: a constant (bvalue) */
	int64_t bvalue;
#define CXX_BRACE_MAX 256
	uint32_t brace_nodes[CXX_BRACE_MAX], brace_n;	/* the last braced list's element nodes (flat lists) */
	uint8_t skip_explicit;		/* the call is an implicit conversion: explicit constructors are not candidates */
	uint8_t construct_depth;	/* conversions through constructors being tried (a constructor's parameter may need one) */
	uint8_t attr_nua, attr_packed;
	uint32_t attr_align;
	uint32_t cpack_name, cpack_at, cpack_n;	/* the last parameter list's pack of a class's bound type pack (As... a), expanded: its name, first parameter, count */
	qty cpack_types[32];
	uint8_t sig_own_pack;
	uint8_t mangle_internal;	/* the last name mangled names an unnamed namespace's or a local entity: internal linkage */		/* the function whose declarator is parsed is an instance with a pack of its own (sig_pack_n) */		/* ... the alignment alignas/aligned asked for (0: none) */	/* the declaration's attributes had no_unique_address, packed */
	uint8_t call_resolved;		/* the last call picked one function (whatever its return type) */
	uint8_t why, none_viable;	/* the last failed deduction's reason; the last call had no viable candidate */
	qty decl_return;		/* the last function declarator's return type (known when a parameter's is not) */			/* the last failed deduction's reason */
	uint32_t body_depth;
	cxx_targ *xargs;		/* the callee's explicit template arguments f<A, B> (when xargs_set) */
	uint32_t nxargs, cxargs;
	uint8_t xargs_set;
	cxx_entity *last_fn;		/* the member function just declared (its deferred body's) */		/* function template bodies being instantiated (nested) */
	uint32_t ftmpl_begin;		/* a template declaration: where the declaration after its parameters starts */
	cxx_scope *ftmpl_scope;		/* ... and its template scope */
	qty this_type;			/* in a member function's body: the class (this is a pointer to it) */
	uint8_t in_template_argument;	/* parsing a template argument's type-id: a trailing ... is an expansion */		/* a lookup found something that depends on template parameters */	/* the class the last nested-name-specifier named, even if dependent */
} cxx;

/* tokens (token.c) */
VPRIV bool cxx_lookahead_fn(cxx *c, void (*fn)(cxx *));	/* expression.c: does fn parse at the current token (tentatively) */
VPRIV void cxx_gen_note_no_node(cxx *c, uint32_t start);	/* codegen/state.c: an expression without a node, the reason */
VPRIV void cxx_gen_write_missing(cxx *c);
VPRIV void cxx_gen_bind_name(cxx *c, cxx_entity *e, uint32_t object, uint32_t field, uint32_t loc);	/* codegen/statement.c: a structured binding's name */
VPRIV void cxx_gen_typeid(cxx *c, qty t, qty ti_type, uint32_t loc);	/* codegen/vtable.c: typeid of a static type */
VPRIV cxx_entity *cxx_entity_prefer(cxx_entity *e, int kind);	/* declaration/name.c: of a set, the entity of that kind */
VPRIV void cxx_gen_base_assign(cxx *c, qty base, uint32_t x, uint32_t loc);	/* codegen/expression.c: B::operator=(x), the implicit one */
VPRIV bool cxx_member_is_extern(cxx *c, const cxx_entity *f);	/* declaration.c: an extern template covers it */
VPRIV bool cxx_type_definitely_incomplete(cxx *c, qty t);	/* instantiate.c: an instance with no definition to complete it */
VPRIV qty cxx_ctad(cxx *c, cxx_entity *te, const qty *args, const uint8_t *lval, uint32_t nargs);	/* declaration.c: Name{args} of a class template */
VPRIV int cxx_gen_trivial_abi(cxx *c, qty t, int depth);	/* codegen/state.c: copied as C does (1), by address (0), not known (-1) */	/* codegen/function.c: VCC_CXX_MISSING, the functions without code */
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
typedef struct cxx_mark { uint32_t pos, nundo; jmp_buf *fail; cxx_scope *scope; uint32_t template_depth, in_template_args; uint8_t dependent_seen; } cxx_mark;
VPRIV void cxx_mark_set(cxx *c, cxx_mark *m);
VPRIV void cxx_mark_rewind(cxx *c, const cxx_mark *m);
VPRIV uint32_t cxx_operator_id_at(cxx *c, uint32_t n);
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
	uint8_t v_short, v_long, v_signed, v_unsigned, v_int128, v_float128, v_float16, v_bf16, v_unknown, quals, v_auto;
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
VPRIV void cxx_class_packed(cxx *c);
VPRIV bool cxx_class_data_size(cxx *c, qty t, uint64_t *dsize);
VPRIV bool cxx_class_base_offset(cxx *c, qty derived, qty base, uint64_t *off, int depth);
VPRIV uint32_t cxx_class_bases(cxx *c, qty t, qty *bases, uint32_t cap);
VPRIV bool cxx_class_field_offset(cxx *c, qty t, uint32_t name, uint64_t *bits, int depth);
VPRIV void cxx_class_skip(cxx *c, int reason);
VPRIV void cxx_class_field(cxx *c, uint32_t name, qty t, uint32_t loc, int bitfield_width);
VPRIV void cxx_class_end(cxx *c);
VPRIV void cxx_class_base(cxx *c, qty t, bool is_virtual);
VPRIV void cxx_class_virtual(cxx *c);
VPRIV bool cxx_class_polymorphic(cxx *c, qty t);
VPRIV bool cxx_class_has_member_initializer(cxx *c, qty t);
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
VPRIV void cxx_template_partial(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t key_pos, uint32_t req_pos);
VPRIV void cxx_template_declared(cxx *c, cxx_entity *e, cxx_scope *params);
VPRIV void cxx_tparam_add(cxx *c, int kind, uint32_t name, bool pack, uint32_t def_b, uint32_t def_e);
VPRIV cxx_entity *cxx_instantiate(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);
VPRIV void cxx_tparams_inherit_defaults(cxx *c, cxx_scope *ps, cxx_scope *in, uint32_t name);
VPRIV const cxx_scope *cxx_ftemplate_params(const struct cxx_ftemplate *f);
VPRIV struct cxx_ftemplate *cxx_ftemplate_new(cxx *c, cxx_scope *params, cxx_scope *home, uint32_t pos, cxx_scope *ctor_class);
VPRIV int cxx_ftemplate_deduce(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
			      uint32_t nargs, qty *ret, bool body);
VPRIV qty cxx_function_signature_parse(cxx *c, bool any_return, bool *body, cxx_scope *ctor_class);
VPRIV void cxx_late_parse_one(cxx *c, const cxx_late *l);
VPRIV void cxx_member_body_use(cxx *c, cxx_entity *e);
VPRIV void cxx_gen_destructor_parts_use(cxx *c, cxx_entity *d);
VPRIV void cxx_gen_defaulted_destructor(cxx *c, cxx_entity *d);
/* the expression is ill-formed for sure (VCC_CXX_SIG=2: where vcc decided so) */
#define CXX_INVALID(c) cxx_invalid_note(c, __FILE__, __LINE__)
static inline void cxx_invalid_note(cxx *c, const char *file, int line)
{
	static __thread int trace = -1;

	if (trace < 0)
		trace = getenv("VCC_CXX_SIG") && atoi(getenv("VCC_CXX_SIG")) > 1;
	c->einvalid = 1, c->einvalid_at = c->pos;
	if (trace)
		fprintf(stderr, "invalid at token %u (%s:%d)\n", c->pos, strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);
}
VPRIV qty cxx_auto_type(cxx *c);
VPRIV cxx_scope *cxx_instance_tparams(cxx *c, qty t);
VPRIV cxx_scope *cxx_template_home(const cxx_entity *te);
VPRIV qty cxx_simple_type_at(cxx *c, uint32_t pos);
VPRIV bool cxx_ftemplate_has_packs(const cxx_entity *e);
VPRIV uint32_t cxx_instance_decl_pos(const cxx_entity *inst);
VPRIV const qty *cxx_instance_placeholders(cxx *c, const cxx_entity *inst, uint32_t *n);
VPRIV bool cxx_instance_uses_decltype(cxx *c, const cxx_entity *inst);
VPRIV qty cxx_instance_declared_sig(cxx *c, const cxx_entity *inst, const qty **ph, uint32_t *nph, uint32_t *pack_param);
VPRIV bool cxx_instance_template(cxx *c, qty t, cxx_entity **of, const cxx_targ **args, uint32_t *nargs, uint32_t *pack_at, cxx_scope **home);
VPRIV bool cxx_mangle_function(cxx *c, const cxx_entity *f, cxx_scope *home, qty ft, bool is_const, int variant, char *buf, size_t cap);
VPRIV bool cxx_gen_on(cxx *c);
VPRIV void cxx_gen_unwind(cxx *c, uint32_t depth);
VPRIV void cxx_gen_fail_at(cxx *c, const char *file, int line);
#define cxx_gen_fail(c) cxx_gen_fail_at(c, __FILE__, __LINE__)
VPRIV bool cxx_gen_type_ok(cxx *c, qty t);
VPRIV void cxx_gen_integer(cxx *c, int64_t v, qty t, uint32_t loc);
VPRIV void cxx_gen_floating(cxx *c, const char *text, size_t len, qty t, uint32_t loc);
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc);
VPRIV void cxx_gen_unary(cxx *c, int op, uint32_t x, uint32_t loc);
VPRIV void cxx_gen_postfix(cxx *c, int op, uint32_t x, uint32_t loc);
VPRIV void cxx_gen_string(cxx *c, const char *text, size_t len, uint32_t loc);
VPRIV void cxx_gen_cast(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV void cxx_gen_index(cxx *c, uint32_t base, uint32_t index, uint32_t loc);
typedef struct cxx_gen_range { cc_id_list pre, body; void *outer; uint32_t p, i, n, loc, cond, step, elem; bool ok, cls; } cxx_gen_range;
VPRIV uint32_t cxx_gen_var_node_public(cxx *c, uint32_t s, uint32_t loc);
VPRIV uint32_t cc_unary_build_public(cxx *c, int op, uint32_t x, uint32_t loc);
VPRIV bool cxx_gen_range_for_begin(cxx *c, cxx_gen_range *r, uint32_t range, const uint32_t *elems, uint32_t nelems, qty elem, uint32_t loc);
VPRIV uint32_t cxx_gen_range_for_element(cxx *c, cxx_gen_range *r);
VPRIV void cxx_gen_range_for_end(cxx *c, cxx_gen_range *r);
VPRIV void cxx_gen_conditional(cxx *c, uint32_t cond, uint32_t x, uint32_t y, uint32_t loc);
VPRIV void cxx_gen_comma(cxx *c, uint32_t x, uint32_t y, uint32_t loc);
VPRIV uint32_t cxx_gen_array_initializer(cxx *c, qty t, uint32_t loc);
VPRIV bool cxx_gen_global_begin(cxx *c, const cxx_entity *v);
VPRIV void cxx_gen_global_end(cxx *c, const cxx_entity *v, uint32_t init, bool has_init);
VPRIV void cxx_gen_member(cxx *c, uint32_t base, bool arrow, uint32_t name, uint32_t loc);
VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV void cxx_gen_ordering_test(cxx *c, uint32_t ord, int op, uint32_t loc);
VPRIV bool cxx_is_initializer_list(cxx *c, qty t);
VPRIV bool cxx_class_converts_public(cxx *c, cxx_scope *cls);
VPRIV void cxx_gen_assignment(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc);
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_this(cxx *c, uint32_t loc);
VPRIV bool cxx_builtin_function_type(cxx *c, const char *name, size_t len, uint32_t nargs, qty arg0, qty *ret);
VPRIV void cxx_gen_builtin(cxx *c, const char *name, size_t len, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_nullptr(cxx *c, uint32_t loc);
VPRIV void cxx_gen_global_object(cxx *c, const cxx_entity *v, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_throw(cxx *c, uint32_t x, bool has_operand, uint32_t loc);
VPRIV uint32_t cxx_gen_typeinfo(cxx *c, qty t);
VPRIV uint32_t cxx_gen_try(cxx *c, uint32_t closure, cxx_entity *call, const uint32_t *ti, uint32_t nhandlers, uint32_t exc, uint32_t loc);
VPRIV uint32_t cxx_gen_scratch(cxx *c, qty t, uint32_t loc);
VPRIV uint32_t cxx_gen_try_statement(cxx *c, uint32_t call, const uint32_t *handlers, uint32_t nhandlers, uint32_t loc);
VPRIV uint32_t cxx_gen_return_zero(cxx *c, uint32_t body, uint32_t loc);
VPRIV bool cxx_try_block_parse(cxx *c, uint32_t *closure_node, cxx_entity **call_out);
VPRIV uint32_t cxx_gen_catch_begin(cxx *c, uint32_t exc, qty decl_type, uint32_t *var, cc_id_list *list, uint32_t loc);
VPRIV bool cxx_mangle_type(cxx *c, qty t, char *buf, size_t cap);
VPRIV bool cxx_gen_lambda_symbol(cxx *c, cxx_entity *call, uint32_t serial);
VPRIV bool cxx_gen_lambda_finish(cxx *c, cxx_gen_lambda *l, cxx_entity *owner, uint32_t *body, uint32_t loc);
VPRIV void cxx_gen_lambda_object(cxx *c, cxx_gen_lambda *l, qty t, uint32_t loc);
VPRIV void cxx_name_type(cxx *c, cxx_entity *e);
VPRIV void cxx_gen_operator_call(cxx *c, uint32_t obj, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_gen_construct_temp(cxx *c, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV void cxx_vtable_declare(cxx *c, cxx_scope *cls, cxx_entity *e, bool virt_kw);
VPRIV void cxx_gen_vtables_emit(cxx *c);
VPRIV void cxx_gen_complete_object_aliases(cxx *c);
VPRIV bool cxx_mangle_variable(cxx *c, const cxx_entity *v, cxx_scope *home, bool internal, char *buf, size_t cap);
VPRIV bool cxx_gen_variable_name(cxx *c, cxx_entity *v, bool internal);
VPRIV bool cxx_scope_in_unnamed_namespace(cxx *c, const cxx_scope *s);
VPRIV bool cxx_mangle_class_data(cxx *c, const char *kind, qty cls, char *buf, size_t cap);
VPRIV void cxx_gen_reinterpret(cxx *c, uint32_t x, qty to, uint32_t loc);
VPRIV void cxx_gen_indirect_call(cxx *c, uint32_t callee, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_static_member_value(cxx *c, const cxx_entity *v, uint32_t loc);
VPRIV void cxx_gen_new(cxx *c, qty t, const uint32_t *place, uint32_t nplace, uint32_t count, bool array, const uint32_t *init, int ninit, uint32_t loc);
VPRIV uint32_t cxx_gen_local_static(cxx *c, uint32_t name, qty t, uint32_t loc);
VPRIV bool cxx_gen_needs_construction_public(cxx *c, qty t);
VPRIV void cxx_gen_destroy_object(cxx *c, uint32_t x, bool arrow, uint32_t loc);
VPRIV void cxx_gen_delete(cxx *c, uint32_t p, bool array, uint32_t loc);
VPRIV qty cxx_std_class(cxx *c, const char *name);
VPRIV void cxx_gen_spaceship(cxx *c, uint32_t l, uint32_t r, qty t, uint32_t loc);
VPRIV void cxx_gen_function_value(cxx *c, cxx_entity *f, uint32_t loc);
VPRIV void cxx_gen_member_initializer(cxx *c, uint32_t name, qty class_type, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV bool cxx_gen_class_ok(cxx *c, qty t);
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV cxx_entity *cxx_member_function_find(cxx *c, cxx_scope *cls, uint32_t name, const cxx_param *ps, uint32_t nps, bool is_const);
VPRIV void cxx_gen_statement(cxx *c, uint32_t node);
VPRIV uint32_t cxx_gen_new_statement(cxx *c, int op, uint32_t loc, uint32_t x, uint32_t y, uint32_t z);
VPRIV uint32_t cxx_gen_condition(cxx *c, uint32_t e);
VPRIV uint32_t cxx_gen_switch_value(cxx *c, uint32_t e);
VPRIV uint32_t cxx_gen_case(cxx *c, int64_t lo, int64_t hi, uint32_t stmt, uint32_t loc);
VPRIV uint32_t cxx_gen_return(cxx *c, uint32_t e, bool has_value, uint32_t loc);
VPRIV uint32_t cxx_gen_local(cxx *c, uint32_t name, qty t, uint32_t loc);
VPRIV void cxx_gen_local_declare(cxx *c, uint32_t s, uint32_t init, bool has_init, uint32_t loc);
VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps);
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body);
VPRIV int cxx_class_special(cxx *c, qty t, int what, bool rvalue, int depth);
VPRIV int cxx_ftemplate_bare_params(cxx *c, cxx_entity *e, uint32_t nargs);
VPRIV void cxx_ftemplate_defined(struct cxx_ftemplate *f);
VPRIV bool cxx_ftemplate_has_body(const cxx_entity *e);
VPRIV uint32_t cxx_ftemplate_pos(const cxx_entity *e);
VPRIV qty cxx_nullptr_type(cxx *c);
VPRIV void cxx_template_name_default_parse(cxx *c, cxx_targ *out);
VPRIV void cxx_template_member_template_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *outer, cxx_scope *inner, cxx_scope *home, uint32_t pos, uint32_t nparams, bool is_const);
VPRIV void cxx_template_member_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *params, cxx_scope *home, uint32_t pos,
					    uint32_t nparams, bool is_const, bool partial);
VPRIV void cxx_vtemplate_special(cxx *c, struct cxx_ftemplate *f, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t init_pos);
VPRIV bool cxx_ftemplate_const_call(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out);
VPRIV bool cxx_vtemplate_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *v);
VPRIV int cxx_concept_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n);
VPRIV void cxx_ftemplate_requires(struct cxx_ftemplate *f, uint32_t pos);
VPRIV cxx_entity *cxx_constraint_name_parse(cxx *c);
VPRIV int cxx_requires_clause_value(cxx *c);
VPRIV void cxx_adl_call_type(cxx *c, uint32_t name, const qty *args, const uint8_t *lval, uint32_t nargs);
VPRIV void cxx_lambda_parameters_parse(cxx *c);
VPRIV void cxx_class_unverifiable(cxx *c);
VPRIV void cxx_for_range_declaration_parse(cxx *c);
VPRIV bool cxx_range_element_type(cxx *c, qty r, qty *elem);
VPRIV qty cxx_auto_deduce(cxx *c, qty pattern, qty a, bool lvalue);
VPRIV void cxx_ec(cxx *c, qty t, int64_t v);
VPRIV bool cxx_type_size_known(cxx *c, qty t, uint64_t *size, uint32_t *align);
VPRIV bool cxx_paren_type_id_follows(cxx *c);
VPRIV bool cxx_class_layout_info(cxx *c, qty t, bool *pod, bool *empty);
VPRIV uint32_t cxx_gen_empty_object(cxx *c, qty t, uint32_t loc);
VPRIV bool cxx_number_value(const cxx_token *t, int64_t *v);
VPRIV bool cxx_binary_value(int op, int64_t l, int64_t r, bool uns, int64_t *v);
VPRIV bool cxx_trait_value(cxx *c, const char *n, size_t len, const qty *t, uint32_t nt, int64_t *v);
VPRIV bool cxx_type_has_placeholder(cxx *c, qty t);
VPRIV cxx_entity *cxx_type_entity(cxx *c, qty t);
VPRIV void cxx_ctor_use(cxx *c, qty t, const qty *args, const uint8_t *lval, uint32_t nargs, bool braced);
VPRIV uint32_t cxx_ctor_id(void), cxx_dtor_id(void);
VPRIV void cxx_instance_complete(cxx *c, cxx_entity *inst);
VPRIV void cxx_type_require_complete(cxx *c, qty t);
VPRIV cxx_scope *cxx_type_class_scope(cxx *c, qty t);
VPRIV cxx_scope *cxx_tag_scope_get(cxx *c, qty t);
VPRIV void cxx_tag_home_set(cxx *c, qty t, cxx_scope *home);
VPRIV cxx_scope *cxx_tag_home(cxx *c, qty t);
VPRIV bool cxx_operator_call_type(cxx *c, uint32_t id, qty obj, const qty *args, uint32_t nargs);
VPRIV void cxx_tag_instance_set(cxx *c, uint32_t tag, void *x);
VPRIV void cxx_tag_info_set(cxx *c, uint32_t tag, void *x);
VPRIV void cxx_pack_element(cxx *c, cxx_entity *e, int32_t i);	/* i < 0: back to the whole pack */
VPRIV void cxx_instance_register(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst);
VPRIV bool cxx_type_spell(cxx *c, qty t, char *buf, size_t cap, size_t *len);
VPRIV void cxx_tag_spelling_set(cxx *c, uint32_t tag, const char *s, size_t n);
VPRIV void cxx_tag_scope_set(cxx *c, uint32_t tag, cxx_scope *s);
VPRIV void cxx_class_specifier_for_instance(cxx *c);
VPRIV bool cxx_template_id_spell(cxx *c, cxx_entity *te, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);

/* expressions' types (typing.c) */
VPRIV void cxx_et(cxx *c, qty t, bool lvalue);
VPRIV qty cxx_number_type(cxx *c, const cxx_token *t);
VPRIV void cxx_binary_type(cxx *c, int op, qty l, qty r);
VPRIV void cxx_unary_type(cxx *c, int op, qty t, bool lvalue);
VPRIV void cxx_call_type(cxx *c, cxx_entity *set, qty callee_type, const qty *args, const uint8_t *lval, uint32_t nargs);
VPRIV void cxx_call_type_x(cxx *c, cxx_entity *set, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
			   uint32_t nargs);

/* parsing (declaration.c, expression.c, statement.c) */
VPRIV void cxx_translation_unit_parse(cxx *c);
VPRIV void cxx_declaration_parse(cxx *c, int context);
VPRIV bool cxx_starts_declaration(cxx *c);
VPRIV void cxx_type_id_parse(cxx *c);
VPRIV uint32_t cxx_handler_declaration_parse(cxx *c);
VPRIV void cxx_type_specifiers_parse(cxx *c);
VPRIV void cxx_new_type_id_parse(cxx *c);
VPRIV bool cxx_try_type_id(cxx *c);
VPRIV void cxx_expression_parse(cxx *c);
VPRIV void cxx_assignment_expression_parse(cxx *c);
VPRIV void cxx_conditional_expression_parse(cxx *c);
VPRIV void cxx_constant_expression_parse(cxx *c);
VPRIV void cxx_initializer_parse(cxx *c);
VPRIV void cxx_braced_init_list_parse(cxx *c);
VPRIV void cxx_call_arguments_parse(cxx *c);
VPRIV void cxx_braced_arguments_parse(cxx *c);
VPRIV void cxx_compound_statement_parse(cxx *c);
VPRIV void cxx_statement_parse(cxx *c);
VPRIV void cxx_template_arguments_parse(cxx *c);
VPRIV void cxx_attributes_skip(cxx *c);
VPRIV void cxx_requires_clause_parse(cxx *c);
VPRIV void cxx_late_parse_all(cxx *c, uint32_t from);
VPRIV cxx_entity *cxx_nested_name_parse(cxx *c, cxx_scope **qualifier, bool *dependent);

enum { CXX_CONTEXT_NAMESPACE = 1, CXX_CONTEXT_CLASS, CXX_CONTEXT_BLOCK, CXX_CONTEXT_TEMPLATE_PARAMETER };
#endif
