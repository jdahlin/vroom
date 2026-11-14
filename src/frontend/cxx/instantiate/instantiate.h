#ifndef CXX_INSTANTIATE_H
#define CXX_INSTANTIATE_H
/*
 * instantiate.c: class and alias template instantiation (PLAN-cxx.md X4),
 * first form.
 *
 * The parser keeps every token and never edits them, so instantiating a
 * template is re-parsing its tokens: a scope binds each parameter to its
 * argument (a type parameter becomes a type entity with that AST type), and
 * the class's head and body (or the alias's type-id) are parsed again from
 * where the template defined them, with no template around.  The result is
 * an ordinary class: its members, its record, its layout.  Function bodies
 * are skipped (types and layouts only, for now).
 *
 * Each instance is kept in a cache keyed by the template and its arguments,
 * the first form of PLAN-cxx.md X5's: an explicit specialization is
 * entered there when it is defined.  What is not handled yet answers
 * "unknown" instead of guessing: a template with partial specializations
 * (matching them is not written), packs, template template parameters,
 * arguments that are not types or integer literals.
 */
#include <stdio.h>
#include "../cxx-internal.h"

static bool cxx_instance_spell(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);

/* a partial specialization: its parameters, its argument pattern's tokens, its class-key */
typedef struct cxx_partial {
	cxx_scope *params, *home;
	uint32_t less_pos, key_pos;	/* the '<' of the pattern X<...>; the class-key */
	uint32_t req_pos;		/* its requires-clause (0: none); its parameters may be constrained too (template<C T>) */
	uint8_t evaluated, ok;		/* the pattern was parsed (ok) */
	uint8_t completed;		/* ... and completed with the primary's default arguments (X<T*> of template<class, class = void>) */
	const cxx_scope *primary, *primary_d;	/* the primary's parameters (and an earlier declaration's): defaults that use them */
	uint8_t subst;			/* ... and an argument of it is not deducible (decltype((void)expr), typename T::x): matched by
					   substituting the deduced parameters and comparing */
	cxx_targ *pattern;		/* the pattern, placeholders for the parameters */
	uint32_t npattern;
	qty *ph;			/* per parameter: its placeholder type (a type parameter) */
} cxx_partial;

struct cxx_template {
	cxx_partial **partials;	/* (each allocated on its own: instances keep pointers to the one they were made from) */
	uint32_t npartials;
	cxx_scope *params;	/* the template scope: its parameters (NULL until defined) */
	cxx_scope *declared;	/* an earlier declaration's template scope: default arguments may be there */
	cxx_scope *home;	/* where the template is declared */
	uint32_t pos, end;	/* class: its class-key token; alias: its type-id [pos, end) */
	uint8_t is_alias, has_partial;
	char *path;		/* its qualified name (the layout dump), NULL: not nameable */
	/* its members defined outside the class: template<...> R X<...>::f(...) { } */
	struct cxx_ooc { uint32_t name, pos, nparams; cxx_scope *params, *home, *inner; uint8_t is_const, partial, pack; } *oocs;	/* (inner: a member template's own parameters) */
	uint32_t noocs;
};

typedef struct cxx_instance {
	cxx_template *t;
	uint32_t nargs;
	cxx_targ *args;
	uint32_t hash;
	cxx_entity *inst;	/* NULL: failed */
	cxx_entity *of;		/* the template */
	cxx_scope *binding;	/* its parameters bound to the arguments */
	uint32_t pos;		/* where its class is re-parsed: the template's, or a partial specialization's class-key */
	uint8_t state;		/* 0 declared (an incomplete record), 1 being completed, 2 complete, 3 failed */
	uint8_t selecting;	/* its partial specialization is being chosen (asked for again meanwhile: not known) */
	uint8_t no_definition;	/* completion found no partial specialization matching and no primary definition */
	cxx_targ *pargs;	/* made from a partial specialization: its parameters' arguments (its members' definitions bind them) */
	uint32_t npargs;
	uint8_t late;		/* named while its template was only declared (using json = basic_json<>;): bound at completion */
	uint8_t missing;	/* an alias whose type named a member that is not there (typename X::type): said again on a cache hit */
} cxx_instance;

typedef struct cxx_saved {
	uint32_t pos, template_depth, in_template_args, implicit_typename, class_depth, nlate, name_path_len, explicit_specialization;
	uint32_t layout_attributes;	/* (an instance's own attributes are not the enclosing class's: alignas in it) */
	cxx_scope *scope;
	jmp_buf *fail;
	struct cxx_class_build *build;
	uint8_t name_path_valid;
	char name_path[512];
	cxx_entity *target, *target2, *last_type;
	uint8_t last_type_dependent, dependent_seen;
	qty ds_type, type_in, type_out;
} cxx_saved;

/* what an enclosing parse still uses while a body is instantiated inside it */
typedef struct cxx_outer {
	cxx_saved s;
	cxx_targ *targs;
	uint32_t ntargs, ndp, ndd;
	cxx_param *dp;
	qty this_type, etype;
	bool elval;
	cxx_entity *ecallee, *last_fn;
	uint32_t unevaluated;
	uint8_t this_const, ret_capture, ret_lval;
	qty ret_type;
	/* code generation: the expression and statement being built, and how many bodies are being built */
	uint32_t enode, snode, sdone, sdepth, gen_stack_depth;
	void *gen_block;
} cxx_outer;

typedef struct cxx_fdone { cxx_targ *args; qty ret, sig; uint8_t body; int8_t viable; uint32_t n; int64_t value; cxx_entity *inst; } cxx_fdone;

/* a variable template's partial or explicit specialization: its pattern and its initializer */
typedef struct cxx_vspec { cxx_partial p; uint32_t init_pos; } cxx_vspec;

typedef struct cxx_ftemplate {
	cxx_partial p;		/* its parameters as placeholders (the partial specializations' machinery) */
	cxx_fdone *done;	/* the argument lists instantiated: their return types (the body parsed once) */
	uint32_t ndone, cdone;
	uint32_t pos;		/* its decl-specifiers */
	cxx_scope *ctor_class;	/* a constructor template: its class (no return type to parse) */
	uint32_t req_pos;	/* its template header's requires-clause (0: none) */
	qty *pt;		/* its parameters' types, placeholders for the template's (one may be unknown: not deduced from) */
	uint32_t np;
	uint8_t has_pt, variadic;
	uint32_t pack_tp, pack_param;	/* a trailing type pack Ts... and the parameter Ts&&... ts that expands it (index + 1; 0: none) */
	uint8_t defined;	/* this declaration has a body (another of the same function may be a declaration only) */
	uint8_t is_friend;	/* a friend declared in a class: its instances are not members (their scope: the class's namespace) */
	cxx_vspec **vspecs;	/* a variable template: its specializations */
	uint32_t nvspecs;
	qty sig;		/* its function type, placeholders for the parameters (CXX_TY_UNKNOWN: not known) */
	uint8_t evaluated, packs;
	cxx_fdone *consts;	/* constexpr calls f<args>() evaluated (cxx_ftemplate_const_call): value, viable 1 known, -1 not */
	uint32_t nconsts, cconsts;
} cxx_ftemplate;

/* the functions of instantiate/, by file */

/* template.c */
VPRIV cxx_template *cxx_template_of(cxx_entity *e);
VPRIV void cxx_template_define_path(cxx *c, cxx_entity *e, cxx_template *t);
VPRIV void cxx_template_define(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t pos, uint32_t end, bool alias);
VPRIV void cxx_template_declared(cxx *c, cxx_entity *e, cxx_scope *params);
VPRIV void cxx_template_partial(cxx *c, cxx_entity *e, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t key_pos, uint32_t req_pos);
VPRIV void cxx_tparam_add(cxx *c, int kind, uint32_t name, bool pack, uint32_t def_b, uint32_t def_e);
VPRIV uint32_t cxx_instance_hash(const cxx_template *t, const cxx_targ *a, uint32_t n);
VPRIV bool cxx_args_equal(const cxx_targ *a, const cxx_targ *b, uint32_t n);
VPRIV cxx_instance *cxx_instance_find(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h);
VPRIV cxx_instance *cxx_instance_add(cxx *c, cxx_template *t, const cxx_targ *a, uint32_t n, uint32_t h);
VPRIV void cxx_instance_put(cxx *c, cxx_template *t, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst);
VPRIV void cxx_instance_register(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, cxx_entity *inst);
VPRIV void cxx_tag_scope_set(cxx *c, uint32_t tag, cxx_scope *s);
VPRIV void cxx_tag_spelling_set(cxx *c, uint32_t tag, const char *s, size_t n);
VPRIV bool cxx_spell_append(char *buf, size_t cap, size_t *len, const char *s);
VPRIV bool cxx_type_spell(cxx *c, qty t, char *buf, size_t cap, size_t *len);

/* binding.c */
VPRIV void cxx_state_save(cxx *c, cxx_saved *s);
VPRIV void cxx_state_restore(cxx *c, const cxx_saved *s);
VPRIV bool cxx_default_argument(cxx *c, const cxx_tparam *p, cxx_scope *binding, cxx_targ *out);
VPRIV qty cxx_bind_value_type(cxx *c, cxx_scope *binding, uint32_t type_b);
VPRIV void cxx_bind(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *a);
VPRIV void cxx_bind_pack(cxx *c, cxx_scope *binding, const cxx_tparam *p, const cxx_targ *list, uint32_t n);
VPRIV void cxx_pack_element(cxx *c, cxx_entity *e, int32_t i);
VPRIV bool cxx_template_id_spell(cxx *c, cxx_entity *te, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);
VPRIV bool cxx_instance_spell(cxx *c, const cxx_template *t, const cxx_targ *a, uint32_t n, char *buf, size_t cap, size_t *len);

/* tag.c */
VPRIV void cxx_tag_grow(void ***arr, uint32_t *n, uint32_t tag);
VPRIV void cxx_tag_info_set(cxx *c, uint32_t tag, void *x);
VPRIV cxx_instance *cxx_tag_info(cxx *c, qty t);
VPRIV bool cxx_instance_template(cxx *c, qty t, cxx_entity **of, const cxx_targ **args, uint32_t *nargs, uint32_t *pack_at, cxx_scope **home);
VPRIV qty cxx_simple_type_at(cxx *c, uint32_t pos);
VPRIV cxx_scope *cxx_template_home(const cxx_entity *te);
VPRIV cxx_scope *cxx_instance_tparams(cxx *c, qty t);

/* match.c */
VPRIV int cxx_placeholder_of(cxx *c, qty t, void **owner);
VPRIV bool cxx_mentions_placeholder(cxx *c, qty t, int depth);
VPRIV bool cxx_args_mention_placeholder(cxx *c, const cxx_targ *a, uint32_t n);
VPRIV bool cxx_match_type(cxx *c, cxx_partial *p, qty pat, qty arg, cxx_targ *bind, int depth);
VPRIV bool cxx_match_list(cxx *c, cxx_partial *p, const cxx_targ *pat, uint32_t np, const cxx_targ *args, uint32_t n, cxx_targ *bind,
			   int depth);
VPRIV bool cxx_match_arg(cxx *c, cxx_partial *p, const cxx_targ *pat, const cxx_targ *arg, cxx_targ *bind, int depth);

/* partial.c */
VPRIV void cxx_partial_evaluate(cxx *c, cxx_partial *p);
VPRIV bool cxx_partial_deduces(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind);
VPRIV int cxx_partial_substitute(cxx *c, cxx_partial *p, const cxx_targ *bind, const cxx_targ *args, uint32_t n);
VPRIV bool cxx_partial_complete(cxx *c, cxx_partial *p, const cxx_scope *ps, const cxx_scope *d, uint32_t n);
VPRIV int cxx_partial_matches(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t n, cxx_targ *bind);
VPRIV bool cxx_partial_at_least_as_specialized(cxx *c, cxx_partial *a, cxx_partial *b);
VPRIV bool cxx_partial_constrained(const cxx_partial *p);
VPRIV int cxx_partial_constraints(cxx *c, cxx_partial *p, const cxx_targ *bind);
VPRIV int cxx_partial_select(cxx *c, cxx_template *t, const cxx_targ *args, uint32_t n, cxx_partial **pick, cxx_targ *bind);

/* instance.c */
VPRIV cxx_entity *cxx_instantiate(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);
VPRIV cxx_entity *cxx_instantiate_now(cxx *c, cxx_entity *te, const cxx_targ *args, uint32_t nargs, bool complete);
VPRIV bool cxx_instance_parse(cxx *c, cxx_instance *x);
VPRIV void cxx_instance_complete(cxx *c, cxx_entity *inst);
VPRIV void cxx_instance_complete_body(cxx *c, cxx_entity *inst);
VPRIV void cxx_type_require_complete(cxx *c, qty t);
VPRIV bool cxx_type_definitely_incomplete(cxx *c, qty t);
VPRIV cxx_scope *cxx_type_class_scope(cxx *c, qty t);
VPRIV void cxx_tag_home_set(cxx *c, qty t, cxx_scope *home);
VPRIV cxx_scope *cxx_tag_home(cxx *c, qty t);
VPRIV cxx_scope *cxx_tag_scope_get(cxx *c, qty t);
VPRIV void cxx_tag_instance_set(cxx *c, uint32_t tag, void *x);

/* ftemplate.c */
VPRIV void cxx_outer_save(cxx *c, cxx_outer *o);
VPRIV void cxx_outer_restore(cxx *c, cxx_outer *o);
VPRIV cxx_entity *cxx_ftemplate_instance(cxx *c, cxx_partial *p, const cxx_targ *args, uint32_t nargs);
VPRIV cxx_ftemplate *cxx_ftemplate_new(cxx *c, cxx_scope *params, cxx_scope *home, uint32_t pos, cxx_scope *ctor_class);
VPRIV void cxx_ftemplate_requires(cxx_ftemplate *f, uint32_t pos);
VPRIV void cxx_ftemplate_friend(cxx_ftemplate *f);
VPRIV bool cxx_ftemplate_has_packs(const cxx_entity *e);
VPRIV qty cxx_instance_declared_sig(cxx *c, const cxx_entity *inst, const qty **ph, uint32_t *nph, uint32_t *pack_param);
VPRIV const qty *cxx_instance_placeholders(cxx *c, const cxx_entity *inst, uint32_t *n);
VPRIV uint32_t cxx_instance_decl_pos(const cxx_entity *inst);
VPRIV bool cxx_instance_uses_decltype(cxx *c, const cxx_entity *inst);
VPRIV const cxx_scope *cxx_ftemplate_params(const cxx_ftemplate *f);
VPRIV void cxx_ftemplate_evaluate(cxx *c, cxx_ftemplate *f);
VPRIV bool cxx_member_template_ooc_body(cxx *c, cxx_ftemplate *f, cxx_entity *inst, const cxx_targ *bind, uint32_t nbind,
					 uint32_t pack_from, qty *ptypes, uint32_t pn);

/* return.c */
VPRIV qty cxx_ftemplate_return(cxx *c, cxx_ftemplate *f, const cxx_targ *bind, bool want_body);
VPRIV qty cxx_ftemplate_return_pack(cxx *c, cxx_ftemplate *f, const cxx_targ *xargs, uint32_t nx, bool want_body, qty *ptypes,
				     uint32_t pn);

/* deduce.c */
VPRIV bool cxx_match_bases(cxx *c, cxx_partial *p, qty P, qty A, cxx_targ *bind, int depth);
VPRIV int cxx_ftemplate_complete(cxx *c, cxx_ftemplate *f, cxx_targ *bind);
VPRIV int cxx_constraint_parse(cxx *c, cxx_partial *p, const cxx_targ *bind, uint32_t pos, const cxx_targ *first);
VPRIV int cxx_ftemplate_constraints(cxx *c, cxx_ftemplate *f, const cxx_targ *bind);
VPRIV bool cxx_instance_of(cxx *c, qty a, const cxx_template *t, int depth);
VPRIV bool cxx_shape_mismatch(cxx *c, qty P, qty A);
VPRIV int cxx_deduce_argument(cxx *c, cxx_partial *p, qty P, qty A, bool lvalue, cxx_targ *bind);
VPRIV qty cxx_placeholder_subst(cxx *c, cxx_partial *p, qty pat, qty t, int depth);
VPRIV int cxx_ftemplate_deduce(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
			      uint32_t nargs, qty *ret, bool body);
VPRIV int cxx_ftemplate_deduce1(cxx *c, cxx_entity *e, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
				 uint32_t nargs, qty *ret, bool body);

/* member.c */
VPRIV void cxx_template_member_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *params, cxx_scope *home, uint32_t pos,
					    uint32_t nparams, bool is_const, bool partial);
VPRIV void cxx_template_member_template_definition(cxx *c, cxx_entity *te, uint32_t name, cxx_scope *outer, cxx_scope *inner,
						     cxx_scope *home, uint32_t pos, uint32_t nparams, bool is_const);
VPRIV bool cxx_ooc_refs_agree(cxx *c, uint32_t pos, const cxx_entity *e);
VPRIV void cxx_member_definition_use(cxx *c, cxx_entity *e);
VPRIV void cxx_member_body_use(cxx *c, cxx_entity *e);
VPRIV void cxx_late_deduce(cxx *c, const cxx_late *l);

/* auto.c */
VPRIV bool cxx_type_has_placeholder(cxx *c, qty t);
VPRIV cxx_entity *cxx_type_entity(cxx *c, qty t);
VPRIV qty cxx_auto_type(cxx *c);
VPRIV qty cxx_auto_subst(cxx *c, qty pat, qty t, int depth);
VPRIV qty cxx_auto_deduce(cxx *c, qty pattern, qty a, bool lvalue);

/* concept.c */
VPRIV int cxx_concept_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n);
VPRIV void cxx_vtemplate_special(cxx *c, cxx_ftemplate *f, cxx_scope *params, cxx_scope *home, uint32_t less_pos, uint32_t init_pos);
VPRIV bool cxx_vtemplate_value(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out);
VPRIV bool cxx_ftemplate_const_call(cxx *c, cxx_entity *e, const cxx_targ *args, uint32_t n, int64_t *out);
VPRIV int cxx_ftemplate_bare_params(cxx *c, cxx_entity *e, uint32_t nargs);
VPRIV int cxx_ftemplate_distinct_bare(cxx *c, cxx_entity *e, uint32_t nargs);
VPRIV void cxx_ftemplate_defined(cxx_ftemplate *f);
VPRIV void cxx_ftemplate_join_declarations(cxx *c, cxx_ftemplate *f, cxx_entity *set, uint32_t nparams, bool is_const);
VPRIV bool cxx_ftemplate_has_body(const cxx_entity *e);
VPRIV bool cxx_ftemplate_constrained(const cxx_entity *e);
VPRIV uint32_t cxx_ftemplate_pos(const cxx_entity *e);
VPRIV uint32_t cxx_ftemplate_np(cxx *c, cxx_ftemplate *f);
VPRIV qty cxx_ftemplate_pt(cxx_ftemplate *f, uint32_t i);

#endif
