/* C++ code generation: the state of the body being built (nesting, failure), which types and classes code is built for */
#include "codegen.h"

#ifndef VCC_UNITY
__thread char cxx_gen_why[256];
#endif

/* building the AST of the function body being parsed? */
VPRIV bool cxx_gen_on(cxx *c)
{
	return c->gen && !c->gen_failed && !c->tentative && !c->template_depth && !c->unevaluated && c->instantiating == c->gen_level;
}

/*
 * The state of the body being built, set aside while another is built inside it: a function template's
 * or a class template instance's member's body is instantiated where it is first used, in the middle of
 * the body that uses it.
 */

typedef struct cxx_gen_saved {
	struct cxx_gen_saved *next;
	uint8_t gen, gen_failed;
	uint32_t gen_level, enode, snode, sdone, sdepth, gen_psyms, gen_npsyms, gen_fn, gen_this, gen_vtt, ast_fn, ast_depth;
	struct cxx_gen_labels *gen_labels;
	void *gen_block, *gen_pre;
	ast_qualified_type gen_ret;
	uint32_t gen_sret, gen_notemp, gen_block_temps;
	uint32_t init_names[32], ninit_names, ninit_bases;
	ast_qualified_type init_bases[16];
	uint8_t delegates, returns, why;
	struct cxx_gen_lambda *gen_lambda;
	char why_text[256];
} cxx_gen_saved;

VPRIV void cxx_gen_push(cxx *c)
{
	cxx_gen_saved *s = malloc(sizeof *s);

	s->next = c->gen_stack;
	s->gen = c->gen, s->gen_failed = c->gen_failed, s->gen_level = c->gen_level;
	s->enode = c->enode, s->snode = c->snode, s->sdone = c->sdone, s->sdepth = c->sdepth;
	s->gen_psyms = c->gen_psyms, s->gen_npsyms = c->gen_npsyms, s->gen_fn = c->gen_fn, s->gen_this = c->gen_this, s->gen_vtt = c->gen_vtt, s->gen_labels = c->gen_labels;
	s->gen_block = c->gen_block, s->gen_pre = c->gen_pre, s->gen_ret = c->gen_ret;
	s->ast_fn = c->ast->fn, s->ast_depth = c->ast->depth;
	s->gen_sret = c->gen_sret, s->gen_notemp = c->gen_notemp, s->gen_block_temps = c->gen_block_temps;
	memcpy(s->init_names, c->gen_init_names, sizeof s->init_names), s->ninit_names = c->ngen_init_names;
	memcpy(s->init_bases, c->gen_init_bases, sizeof s->init_bases), s->ninit_bases = c->ngen_init_bases;
	s->delegates = c->gen_delegates, s->returns = c->gen_returns, s->why = c->gen_why;
	s->gen_lambda = c->gen_lambda;
	memcpy(s->why_text, cxx_gen_why, sizeof cxx_gen_why), cxx_gen_why[0] = 0;
	c->gen_stack = s;
	c->gen_stack_depth++;
}

VPRIV void cxx_gen_pop(cxx *c)
{
	cxx_gen_saved *s = c->gen_stack;

	c->gen_stack = s->next;
	c->gen_stack_depth--;
	memcpy(cxx_gen_why, s->why_text, sizeof cxx_gen_why);
	c->gen = s->gen, c->gen_failed = s->gen_failed, c->gen_level = s->gen_level;
	c->enode = s->enode, c->snode = s->snode, c->sdone = s->sdone, c->sdepth = s->sdepth;
	c->gen_psyms = s->gen_psyms, c->gen_npsyms = s->gen_npsyms, c->gen_fn = s->gen_fn, c->gen_this = s->gen_this, c->gen_vtt = s->gen_vtt, c->gen_labels = s->gen_labels;
	c->gen_block = s->gen_block, c->gen_ret = s->gen_ret;
	c->gen_sret = s->gen_sret, c->gen_notemp = s->gen_notemp, c->gen_block_temps = s->gen_block_temps;
	memcpy(c->gen_init_names, s->init_names, sizeof s->init_names), c->ngen_init_names = s->ninit_names;
	memcpy(c->gen_init_bases, s->init_bases, sizeof s->init_bases), c->ngen_init_bases = s->ninit_bases;
	c->gen_delegates = s->delegates, c->gen_returns = s->returns, c->gen_why = s->why;
	c->gen_lambda = s->gen_lambda;
	if (c->gen_pre != s->gen_pre)
		free(c->gen_pre);
	c->gen_pre = s->gen_pre;
	c->ast->fn = s->ast_fn;
	while (c->ast->depth > s->ast_depth)
		cc_scope_close(c->ast);
	free(s);
}

/* the bodies begun since `depth` were abandoned (their parse failed): back to the one that was being built */
VPRIV void cxx_gen_unwind(cxx *c, uint32_t depth)
{
	while (c->gen_stack_depth > depth)
		cxx_gen_pop(c);
}

/* the body has something code generation does not build yet */
VPRIV void cxx_gen_fail_at(cxx *c, const char *file, int line)
{
	static __thread int trace = -1;

	if (!c->gen || c->tentative || c->template_depth || c->unevaluated || c->instantiating != c->gen_level)
		return;	/* (unevaluated: a discarded if constexpr branch, sizeof's operand) */
	if (trace < 0)
		trace = getenv("VCC_CXX_GEN") != NULL || getenv("VCC_CXX_MISSING") != NULL;
	if (trace && !c->gen_failed && !cxx_gen_why[0]) {	/* VCC_CXX_GEN=4: kept for the function's name (cxx_gen_function_end) */
		int k = snprintf(cxx_gen_why, sizeof cxx_gen_why, "%s:%d:", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);

		for (uint32_t i = c->pos > 6 ? c->pos - 6 : 0; i < c->pos + 3 && i < c->ntok && k > 0 && k < (int)sizeof cxx_gen_why - 40; i++)
			k += snprintf(cxx_gen_why + k, sizeof cxx_gen_why - k, " %.*s", (int)(c->tok[i].len < 30 ? c->tok[i].len : 30), c->tok[i].s);
	}
	if (trace && !c->gen_failed && getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) < 4) {	/* VCC_CXX_GEN=1: the first thing in each function that is not built, and where vcc gave up */
		fprintf(stderr, "no code (%s:%d):", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);
		for (uint32_t i = c->pos > 10 ? c->pos - 10 : 0; i < c->pos + 3 && i < c->ntok; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	}
	c->gen_failed = 1;
}

VPRIV bool cxx_gen_object_type_ok(cxx *c, ast_qualified_type t);
VPRIV uint32_t cxx_gen_vptr_store_at(cxx *c, cxx_scope *cls, uint32_t self, uint32_t loc);
VPRIV uint32_t cxx_gen_dtor_sym(const cxx_entity *d);
VPRIV uint32_t cxx_gen_nullptr_object(cxx *c, uint32_t x, uint32_t loc);
VPRIV bool cxx_gen_is_nullptr_node(cxx *c, uint32_t x);
VPRIV uint32_t cxx_gen_initializer_list(cxx *c, ast_qualified_type ilt, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc, uint32_t *array);
VPRIV bool cxx_vtable_usable(cxx *c, cxx_scope *cls);
VPRIV bool cxx_gen_value_ok(cxx *c, ast_qualified_type t);
VPRIV bool cxx_gen_by_address(cxx *c, ast_qualified_type t);
VPRIV int cxx_gen_trivial_abi(cxx *c, ast_qualified_type t, int depth);
VPRIV bool cxx_gen_needs_destruction(cxx *c, ast_qualified_type t, int depth);
VPRIV bool cxx_gen_needs_construction(cxx *c, ast_qualified_type t, int depth);
VPRIV cxx_entity *cxx_gen_declared_destructor(cxx *c, ast_qualified_type t);
VPRIV cxx_entity *cxx_gen_effective_destructor(cxx *c, ast_qualified_type t, int depth);
VPRIV cxx_entity *cxx_gen_declared_destructor(cxx *c, ast_qualified_type t);
VPRIV cxx_entity *cxx_gen_destructor_body(cxx *c, ast_qualified_type t, cxx_entity *d);
VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps);
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body);
VPRIV bool cxx_gen_construct_at(cxx *c, uint32_t target, ast_qualified_type t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);
VPRIV bool cxx_gen_construct_by_body(cxx *c, uint32_t target, ast_qualified_type t, cxx_entity *d, cc_id_list *out, uint32_t loc);
VPRIV bool cxx_gen_name(cxx *c, cxx_entity *f);
VPRIV int cxx_gen_capture(cxx *c, cxx_gen_lambda *l, const cxx_entity *v, uint32_t loc);
VPRIV ast_qualified_type cxx_gen_abi_type(cxx *c, const cxx_entity *f);
VPRIV uint32_t cxx_gen_text(cxx *c, const char *text);
VPRIV uint32_t cxx_gen_data_address(cxx *c, uint32_t s, uint32_t index);
VPRIV uint32_t cxx_gen_function_node(cxx *c, uint32_t s, uint32_t loc);
VPRIV void cxx_gen_typeinfos_emit(cxx *c);

/* is init a constant initializer for an object of type ty (cc_initializer_static_is_ok reports where not) */
VPRIV bool cxx_gen_static_ok(cc *a, ast_qualified_type ty, uint32_t init)
{
	uint32_t bad = 0;

	return cc_initializer_static_is_ok(a, ty, init, &bad);
}
VPRIV void cxx_gen_file_asm(cxx *c, const char *text);
VPRIV uint32_t cxx_gen_data_symbol(cxx *c, const char *name, uint32_t n);
VPRIV uint32_t cxx_gen_to_void_pointer(cxx *c, uint32_t n);
VPRIV uint32_t cxx_gen_runtime_call(cxx *c, uint32_t fs, const uint32_t *args, uint32_t nargs, uint32_t loc);

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);
VPRIV bool cxx_gen_is_reference(cxx *c, ast_qualified_type t);
VPRIV uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, ast_qualified_type to, uint32_t loc);
VPRIV uint32_t cxx_gen_base_object(cxx *c, uint32_t x, ast_qualified_type to, uint32_t loc);

/*
 * A type code is generated for so far, as a value (a parameter, a result, an operand): a scalar, a
 * reference to an object type that is, or a class that C's rules pass and copy as C++'s do (laid out,
 * POD for layout: no constructor, destructor or assignment of its own).
 */
VPRIV bool cxx_gen_type_ok(cxx *c, ast_qualified_type t)
{
	int k;
	bool pod, empty;

	if (t == CXX_TY_UNKNOWN)
		return false;
	k = AST_TYPE_KIND(c->ast, t);
	if (k == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext) {
		/* a reference is an address, as a pointer is: a class it refers to need not be one code is built for
		   (std::ostream &, a virtual base's class); what is done through it is checked where it is done */
		ast_qualified_type b = AST_TYPE(c->ast, t)->base;

		if (b != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, b) && AST_TYPE_KIND(c->ast, b) != AST_TYPE_ARRAY)
			return true;
		if (b != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, b) == AST_TYPE_FUNCTION || AST_TYPE_KIND(c->ast, b) == AST_TYPE_ARRAY))
			return true;	/* passing an array reference needs only its address, even with an unknown bound */
		return cxx_gen_object_type_ok(c, b);
	}
	if (k == AST_TYPE_STRUCT || k == AST_TYPE_UNION)
		return cxx_class_layout_info(c, t, &pod, &empty) && pod &&	/* (an instance is completed here if it was not yet) */
		       (c->ast->tags[AST_TYPE(c->ast, t)->aux].flags & AST_RECORD_FLAG_COMPLETE);
	return (k >= AST_TYPE_BOOL && k <= AST_TYPE_UINT128) || k == AST_TYPE_FLOAT || k == AST_TYPE_DOUBLE || k == AST_TYPE_LONG_DOUBLE || k == AST_TYPE_POINTER || k == AST_TYPE_ENUM;
}

/*
 * A class whose objects code is generated for: laid out, its bases and its members of class type such
 * classes too, its other members scalars or arrays of them.  It may declare constructors and a
 * destructor; passing it by value is then not built yet (cxx_gen_type_ok).
 */
VPRIV bool cxx_gen_class_ok2(cxx *c, ast_qualified_type t, bool objects);

__attribute__((unused)) VPRIV bool cxx_gen_class_ok(cxx *c, ast_qualified_type t)
{
	return cxx_gen_class_ok2(c, t, false);
}

/* objects: as an object in memory (a class with virtual functions too: never copied as a value here) */
VPRIV bool cxx_gen_class_ok2(cxx *c, ast_qualified_type t, bool objects)
{
	cc *a = c->ast;
	cxx_scope *cls;
	bool pod, empty;

#define CK(x, why) do { if (x) return false; } while (0)	/* (why: what VCC_CXX_GEN would say) */
	CK(t == CXX_TY_UNKNOWN || (AST_TYPE_KIND(a, t) != AST_TYPE_STRUCT && AST_TYPE_KIND(a, t) != AST_TYPE_UNION), "kind");
	CK(!cxx_class_layout_info(c, t, &pod, &empty), "layout");
	CK(!(a->tags[AST_TYPE(a, t)->aux].flags & AST_RECORD_FLAG_COMPLETE), "incomplete");
	if (pod)
		return true;
	cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	CK(!cls || cls->has_dependent_base, "dependent base");
	CK(cxx_class_polymorphic(c, t) && (!objects || !cxx_vtable_usable(c, cls)), "vtable");
	{
		ast_qualified_type bases[16];
		uint32_t nb = cxx_class_bases(c, t, bases, 16);

		for (uint32_t i = 0; i < nb; i++)
			if (!cxx_gen_class_ok2(c, bases[i], objects))
				return false;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft)) {
			if (!cxx_gen_class_ok2(c, ft, objects))
				return false;
		} else
			CK(!cxx_gen_type_ok(c, ft), "field type");	/* (a reference member is a pointer in the object: its referent's type must do) */
	}
	return true;
}

/* ... or an array of them (a variable's type; a parameter's has decayed), or such a class */
VPRIV bool cxx_gen_object_type_ok(cxx *c, ast_qualified_type t)
{
	while (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY && AST_TYPE(c->ast, t)->n)
		t = AST_TYPE(c->ast, t)->base;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY)
		return cxx_gen_class_ok2(c, t, true);
	return t != CXX_TY_UNKNOWN && !(AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext) && cxx_gen_type_ok(c, t);
}

VPRIV bool cxx_gen_is_reference(cxx *c, ast_qualified_type t)
{
	return t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext;
}

/* a temporary of the function body: a local of type t, declared in the block being built (0: no block) */
VPRIV uint32_t cxx_gen_temporary(cxx *c, ast_qualified_type t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t s, n;

	if (!c->gen_block) {
		cxx_gen_fail(c);
		return 0;
	}
	s = cc_symbol_new(a, AST_SYMBOL_VARIABLE, 0, AST_QUALIFIED_TYPE_UNQUALIFIED(t), loc);
	a->syms[s].sc = AST_STORAGE_AUTO;
	n = cc_node_new_operands(a, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, s, 0);
	cc_id_list_append(a, (cc_id_list *)c->gen_block, n);
	return s;
}

/* does the class, or a class inside it, have a base class that takes space?  (C's view of the record has
   plain bytes there: its classification for registers would not be the ABI's) */
VPRIV bool cxx_gen_has_base_bytes(cxx *c, ast_qualified_type t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	ast_qualified_type bases[16];
	uint32_t nb;
	bool pod, empty;

	if (!cls || depth > 8)
		return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (!cxx_class_layout_info(c, bases[i], &pod, &empty) || !empty)
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) && cxx_gen_has_base_bytes(c, ft, depth + 1))
			return true;
	}
	return false;
}

/*
 * Is the class trivial for the purposes of calls (the Itanium ABI): no copy or move constructor or
 * destructor that is not trivial, not all of them deleted?  1 yes (passed and returned as C does), 0 no
 * (by address), -1 not known.
 */
VPRIV int cxx_gen_trivial_abi(cxx *c, ast_qualified_type t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	cxx_entity *d;
	ast_qualified_type bases[16];
	uint32_t nb, deleted = 0, usable = 0;
	bool pod, empty;

	if (!cxx_class_layout_info(c, t, &pod, &empty))
		return -1;
	if (pod)
		return 1;
	if (!cls || depth > 8)
		return -1;
	if (cxx_class_polymorphic(c, t))
		return 0;
	d = cxx_gen_declared_destructor(c, t);
	if (d && !d->fn_defaulted)
		return 0;
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next) {
		const ctype *ft;
		ast_qualified_type p;

		if (e->member_of != cls || e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, e->ty) != AST_TYPE_FUNCTION)
			continue;
		ft = AST_TYPE(a, e->ty);
		if (!ft->n || ft->n > (uint32_t)e->ndefaults + 1)
			continue;
		p = a->params[ft->aux];
		if (p == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, p) != AST_TYPE_POINTER || !AST_TYPE(a, p)->ext || AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(a, p)->base) != AST_QUALIFIED_TYPE_UNQUALIFIED(t))
			continue;
		/* a copy or move constructor */
		if (e->fn_deleted)
			deleted++;
		else if (!e->fn_defaulted)
			return 0;
		else
			usable++;
	}
	if (deleted && !usable)
		return 0;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		int r = cxx_gen_trivial_abi(c, bases[i], depth + 1);

		if (r <= 0)
			return r;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN)
			return -1;
		if (cc_type_is_record(a, ft)) {
			int r = cxx_gen_trivial_abi(c, ft, depth + 1);

			if (r <= 0)
				return r;
		}
	}
	return 1;
}

/* a class passed and returned by address */
VPRIV bool cxx_gen_by_address(cxx *c, ast_qualified_type t)
{
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY)
		return false;
	return cxx_gen_trivial_abi(c, t, 0) == 0;
}

/* a type as a parameter or a result: cxx_gen_type_ok's, and classes with constructors and destructors */
/* does the class hold floating-point data (its members, bases', arrays'): a base's bytes would pass it in
   integer registers where the ABI uses SSE ones */
static bool cxx_gen_has_floating(cxx *c, ast_qualified_type t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	ast_qualified_type bases[16];
	uint32_t nb;

	if (!cls || depth > 8)
		return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_has_floating(c, bases[i], depth + 1))
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN)
			return true;
		if (cc_type_is_record(a, ft) ? cxx_gen_has_floating(c, ft, depth + 1) :
		    (AST_TYPE_KIND(a, ft) >= AST_TYPE_BFLOAT16 && AST_TYPE_KIND(a, ft) <= AST_TYPE_FLOAT128) || AST_TYPE_KIND(a, ft) == AST_TYPE_VECTOR)
			return true;
	}
	return false;
}

VPRIV bool cxx_gen_value_ok(cxx *c, ast_qualified_type t)
{
	cc *a = c->ast;
	int tr;

	if (cxx_gen_type_ok(c, t))
		return true;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(a, t) && AST_TYPE_KIND(a, t) != AST_TYPE_ARRAY)
		cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));	/* (a result type an instance names, tuple<int&>: completed now) */
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(a, t) || AST_TYPE_KIND(a, t) == AST_TYPE_ARRAY || !cxx_gen_class_ok2(c, t, true))
		return false;
	tr = cxx_gen_trivial_abi(c, t, 0);
	if (tr == 1)
		return AST_TYPE(a, t)->size > 16 || !cxx_gen_has_base_bytes(c, t, 0) || !cxx_gen_has_floating(c, t, 0);
	return tr == 0 && a->o->target == PP_LINUX_X86_64;	/* (the convention is written out for x86-64 only so far) */
}

/* ---- class prvalues and temporaries ---------------------------------------------------------- */

VPRIV cxx_prv *cxx_gen_prv_find(cxx *c, uint32_t node)
{
	for (uint32_t i = c->nprvs; node && i-- > 0 && c->nprvs - i <= 64;)
		if (c->prvs[i].node == node)
			return &c->prvs[i];
	return NULL;
}

VPRIV void cxx_gen_prv_add(cxx *c, uint32_t node, uint32_t expr, uint32_t call, uint32_t tmp)
{
	if (c->nprvs == c->cprvs)
		c->prvs = realloc(c->prvs, (c->cprvs = c->cprvs ? c->cprvs * 2 : 64) * sizeof *c->prvs);
	c->prvs[c->nprvs++] = (cxx_prv){ node, expr, call, tmp, 0 };
}

/* the prvalue is constructed in the object at `address` instead of its temporary (copy elision): the
   expression that does it (the temporary is left unused, and is not destroyed) */
VPRIV uint32_t cxx_gen_prv_aim(cxx *c, cxx_prv *p, uint32_t address)
{
	cc *a = c->ast;
	sym *y = &a->syms[p->tmp];
	uint32_t expr = p->expr;

	a->lists[AST_NODE_AT(a, p->call)->b] = address;
	if (y->attrs && a->attr[y->attrs].cleanup) {
		a->attr[y->attrs].cleanup = 0;
		if (c->gen_block_temps)
			c->gen_block_temps--;
	}
	p->node = 0;
	return expr;
}

/*
 * A temporary of class type t, declared in the block being built and destroyed at that block's end (later
 * than C++ says, the end of the full-expression).  0: not built: no destructor to call, or a place that
 * runs more than once or not at all for one entry of the block (a loop's condition, x && y's y).
 */
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body);
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);
VPRIV uint32_t cxx_gen_text(cxx *c, const char *text);

/*
 * static void __vcc_cdtor_N(T **live) { if (*live) (*live)->~T(); }: the cleanup of a temporary that an
 * operand which may not run builds (a ? b : T(x), a && f(T(x))): *live is its address once it is built,
 * null until then.  0: not built.
 */
static uint32_t cxx_gen_cond_dtor(cxx *c, ast_qualified_type t, cxx_entity *dtor)
{
	cc *a = c->ast;
	uint32_t tag = AST_TYPE(a, t)->aux, body, p, inner, call, s;
	ast_qualified_type pp = cc_type_pointer(a, cc_type_pointer(a, t));
	cxx_entity *f, *pe;
	cxx_scope *fs;
	cxx_param ps[1];
	char nm[64];
	attrs at;
	cc_id_list l;

	for (uint32_t i = 0; i < c->gen_ncdtor; i++)
		if (c->gen_cdtor_tag[i] == tag)
			return c->gen_cdtor_sym[i];
	f = calloc(1, sizeof *f);
	f->kind = CXX_ENTITY_VALUE, f->name = vintern_cstr("__vcc_cdtor"), f->pack_index = -1;
	f->ty = cxx_type_function(c, cc_type_basic(AST_TYPE_VOID), &pp, 1, false);
	f->fn_internal = 1, f->mangled = 1;
	f->sym = cc_symbol_new(a, AST_SYMBOL_FUNCTION, f->name, f->ty, 0);
	a->syms[f->sym].depth = 0;
	a->syms[f->sym].sc = AST_STORAGE_STATIC;
	snprintf(nm, sizeof nm, "__vcc_cdtor_%u", c->gen_ncdtor);
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, nm) + 1;
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, c->global, NULL);
	ps[0].name = vintern_cstr("live"), ps[0].ty = pp;
	pe = cxx_declare(c, fs, ps[0].name, CXX_ENTITY_VALUE);
	if (!pe)
		return 0;
	pe->ty = pp;
	if (!cxx_gen_function_begin(c, f, fs, ps, 1))
		return 0;
	p = cxx_gen_var_node(c, pe->sym, 0);
	inner = cc_node_rvalue(a, cc_dereference_build(a, cc_node_rvalue(a, p), 0));
	cxx_gen_call(c, dtor, cc_dereference_build(a, inner, 0), false, NULL, 0, 0);
	call = c->enode, c->enode = 0;
	if (!call) {
		c->gen_failed = 1;
		cxx_gen_function_end(c, f, 0);
		return 0;
	}
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, AST_NODE_IF, 0, cc_condition_scalar(a, cc_node_rvalue(a, cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, pe->sym, 0)), 0)), "statement"),
						  cc_statement_new(a, AST_NODE_EXPRESSION, 0, call, 0, 0), 0));
	body = cc_statement_new(a, AST_NODE_BLOCK, 0, 0, cc_id_list_freeze(a, &l), l.n);
	cxx_gen_function_end(c, f, body);
	if (!(a->syms[f->sym].flags & AST_SYMBOL_FLAG_DEFINED))
		return 0;
	s = f->sym;
	c->gen_cdtor_tag = realloc(c->gen_cdtor_tag, (c->gen_ncdtor + 1) * sizeof *c->gen_cdtor_tag);
	c->gen_cdtor_sym = realloc(c->gen_cdtor_sym, (c->gen_ncdtor + 1) * sizeof *c->gen_cdtor_sym);
	c->gen_cdtor_tag[c->gen_ncdtor] = tag, c->gen_cdtor_sym[c->gen_ncdtor++] = s;
	return s;
}

VPRIV uint32_t cxx_gen_class_temp(cxx *c, ast_qualified_type t, uint32_t loc)
{
	cc *a = c->ast;
	bool needs = cxx_gen_needs_destruction(c, t, 0);
	cxx_entity *dtor = needs ? cxx_gen_effective_destructor(c, t, 0) : NULL;
	uint32_t s;
	bool cond = c->gen_cond_ok;

	c->gen_cond_ok = 0, c->gen_cond_live = 0;
	if (needs && (!dtor || dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1 || (cxx_member_body_use(c, dtor), 0)))
		return 0;
	if (needs && c->gen_notemp) {
		/* in an operand that may not run: T tmp; T *live = 0; (live = &tmp once built), destroyed through
		   live (the caller sets it: cxx_gen_construct_temp) */
		uint32_t helper, live, n;
		attrs at;

		if (!cond || !c->gen_block || !(helper = cxx_gen_cond_dtor(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t), dtor)))
			return 0;
		s = cxx_gen_temporary(c, t, loc);
		if (!s)
			return 0;
		live = cc_symbol_new(a, AST_SYMBOL_VARIABLE, 0, cc_type_pointer(a, AST_QUALIFIED_TYPE_UNQUALIFIED(t)), loc);
		a->syms[live].sc = AST_STORAGE_AUTO;
		n = cc_node_new_operands(a, AST_NODE_DECLARATION, cc_type_basic(AST_TYPE_VOID), loc, live,
					 cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(AST_TYPE_LONG), loc), a->syms[live].ty, AST_CAST_INTEGER_TO_POINTER));
		cc_id_list_append(a, (cc_id_list *)c->gen_block, n);
		memset(&at, 0, sizeof at);
		at.cleanup = helper;
		cc_attribute_apply_symbol(a, &a->syms[live], &at);
		c->gen_block_temps++;
		c->gen_cond_live = live;
		return s;
	}
	if (!c->gen_block)
		return 0;
	s = cxx_gen_temporary(c, t, loc);
	if (s && needs) {
		attrs at;

		memset(&at, 0, sizeof at);
		at.cleanup = cxx_gen_dtor_sym(dtor);
		cc_attribute_apply_symbol(a, &a->syms[s], &at);
		c->gen_block_temps++;
	}
	return s;
}

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

/* the statements' expressions (each an expression statement) as one expression, then `last` */
VPRIV uint32_t cxx_gen_chain(cxx *c, const cc_id_list *l, uint32_t last, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t x = 0;

	for (uint32_t i = 0; i < l->n; i++) {
		const struct node *st = AST_NODE_AT(a, l->v[i]);
		uint32_t e;

		if (st->op == AST_NODE_DECLARATION && !st->b && c->gen_block && (cc_id_list *)c->gen_block != l) {
			/* a local the statements declare (an initializer_list's array): declared in the enclosing block */
			cc_id_list_append(a, (cc_id_list *)c->gen_block, l->v[i]);
			continue;
		}
		if (st->op != AST_NODE_EXPRESSION) {
			cxx_gen_fail(c);	/* (a statement that is no expression: not one expression's part) */
			return 0;
		}
		e = st->a;
		x = x ? cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, e)->ty, loc, x, e) : e;
	}
	if (!last)
		return x;
	return x ? cc_node_new_operands(a, AST_NODE_COMMA, AST_NODE_AT(a, last)->ty, loc, x, last) : last;
}

/* an expression code generation gave no node (tokens start to here): the function's reason, unless one is
   known (VCC_CXX_GEN=4, VCC_CXX_MISSING: the innermost cause, not the statement that then has no node) */
VPRIV void cxx_gen_note_no_node(cxx *c, uint32_t start)
{
	int k;

	if (c->gen_failed || cxx_gen_why[0])
		return;
	k = snprintf(cxx_gen_why, sizeof cxx_gen_why, "no node:");
	for (uint32_t i = start; i < c->pos && i < start + 14 && k > 0 && k < (int)sizeof cxx_gen_why - 50; i++)
		k += snprintf(cxx_gen_why + k, sizeof cxx_gen_why - k, " %.*s", (int)(c->tok[i].len < 30 ? c->tok[i].len : 30), c->tok[i].s);
	if (k > 0 && k < (int)sizeof cxx_gen_why - 12)
		snprintf(cxx_gen_why + k, sizeof cxx_gen_why - k, " -> %s", c->etype == CXX_TY_UNKNOWN ? "untyped" :
			 cc_type_is_record(c->ast, c->etype) ? "class" : "typed");
}
