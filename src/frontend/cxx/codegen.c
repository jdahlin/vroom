/*
 * C++ code generation, first form (PLAN-cxx.md): while a function body is parsed the C++ parser builds
 * the shared AST for it with the C front end's builders (the same nodes, conversions and statements C
 * gives the emitter), and cxx_run hands the TU to emit_tu.  What is built so far: functions at namespace
 * scope over scalar types (integers, floating types, pointers): literals, variables, the built-in
 * operators, calls of such functions, if/while/do/return/break/continue, local variables.  A function
 * whose body has anything else is left undefined (c->gen_failed): declared, not emitted.
 */
#include "cxx-internal.h"

#define AST_NODE_AT(a, i) (&(a)->nodes[i])

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
static __thread char cxx_gen_why[256];	/* VCC_CXX_GEN=4: the first thing not built in the function being built */

typedef struct cxx_gen_saved {
	struct cxx_gen_saved *next;
	uint8_t gen, gen_failed;
	uint32_t gen_level, enode, snode, sdone, sdepth, gen_psyms, gen_npsyms, gen_fn, gen_this, ast_fn, ast_depth;
	void *gen_block, *gen_pre;
	qty gen_ret;
	uint32_t gen_sret, gen_notemp, gen_block_temps;
	uint32_t init_names[32], ninit_names, ninit_bases;
	qty init_bases[16];
	uint8_t delegates, returns, why;
	struct cxx_gen_lambda *gen_lambda;
	char why_text[256];
} cxx_gen_saved;

static void cxx_gen_push(cxx *c)
{
	cxx_gen_saved *s = malloc(sizeof *s);

	s->next = c->gen_stack;
	s->gen = c->gen, s->gen_failed = c->gen_failed, s->gen_level = c->gen_level;
	s->enode = c->enode, s->snode = c->snode, s->sdone = c->sdone, s->sdepth = c->sdepth;
	s->gen_psyms = c->gen_psyms, s->gen_npsyms = c->gen_npsyms, s->gen_fn = c->gen_fn, s->gen_this = c->gen_this;
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

static void cxx_gen_pop(cxx *c)
{
	cxx_gen_saved *s = c->gen_stack;

	c->gen_stack = s->next;
	c->gen_stack_depth--;
	memcpy(cxx_gen_why, s->why_text, sizeof cxx_gen_why);
	c->gen = s->gen, c->gen_failed = s->gen_failed, c->gen_level = s->gen_level;
	c->enode = s->enode, c->snode = s->snode, c->sdone = s->sdone, c->sdepth = s->sdepth;
	c->gen_psyms = s->gen_psyms, c->gen_npsyms = s->gen_npsyms, c->gen_fn = s->gen_fn, c->gen_this = s->gen_this;
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
		trace = getenv("VCC_CXX_GEN") != NULL;
	if (trace && !c->gen_failed) {	/* VCC_CXX_GEN=4: kept for the function's name (cxx_gen_function_end) */
		int k = snprintf(cxx_gen_why, sizeof cxx_gen_why, "%s:%d:", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);

		for (uint32_t i = c->pos > 6 ? c->pos - 6 : 0; i < c->pos + 3 && i < c->ntok && k > 0 && k < (int)sizeof cxx_gen_why - 40; i++)
			k += snprintf(cxx_gen_why + k, sizeof cxx_gen_why - k, " %.*s", (int)(c->tok[i].len < 30 ? c->tok[i].len : 30), c->tok[i].s);
	}
	if (trace && !c->gen_failed && atoi(getenv("VCC_CXX_GEN")) < 4) {	/* VCC_CXX_GEN=1: the first thing in each function that is not built, and where vcc gave up */
		fprintf(stderr, "no code (%s:%d):", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);
		for (uint32_t i = c->pos > 10 ? c->pos - 10 : 0; i < c->pos + 3 && i < c->ntok; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	}
	c->gen_failed = 1;
}

static bool cxx_gen_object_type_ok(cxx *c, qty t);
static uint32_t cxx_gen_dtor_sym(const cxx_entity *d);
static uint32_t cxx_gen_initializer_list(cxx *c, qty ilt, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc, uint32_t *array);
static bool cxx_vtable_usable(cxx *c, cxx_scope *cls);
static bool cxx_gen_value_ok(cxx *c, qty t);
static bool cxx_gen_by_address(cxx *c, qty t);
static int cxx_gen_trivial_abi(cxx *c, qty t, int depth);
static bool cxx_gen_needs_destruction(cxx *c, qty t, int depth);
static bool cxx_gen_needs_construction(cxx *c, qty t, int depth);
static cxx_entity *cxx_gen_declared_destructor(cxx *c, qty t);
static cxx_entity *cxx_gen_effective_destructor(cxx *c, qty t, int depth);
static cxx_entity *cxx_gen_declared_destructor(cxx *c, qty t);
static cxx_entity *cxx_gen_destructor_body(cxx *c, qty t, cxx_entity *d);
VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps);
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body);
static bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);
static bool cxx_gen_construct_by_body(cxx *c, uint32_t target, qty t, cxx_entity *d, cc_id_list *out, uint32_t loc);
static bool cxx_gen_name(cxx *c, cxx_entity *f);
static int cxx_gen_capture(cxx *c, cxx_gen_lambda *l, const cxx_entity *v, uint32_t loc);
static qty cxx_gen_abi_type(cxx *c, const cxx_entity *f);
static uint32_t cxx_gen_text(cxx *c, const char *text);
static uint32_t cxx_gen_data_address(cxx *c, uint32_t s, uint32_t index);
static uint32_t cxx_gen_function_node(cxx *c, uint32_t s, uint32_t loc);
static void cxx_gen_typeinfos_emit(cxx *c);

/* is init a constant initializer for an object of type ty (cc_initializer_static_is_ok reports where not) */
static bool cxx_gen_static_ok(cc *a, qty ty, uint32_t init)
{
	uint32_t bad = 0;

	return cc_initializer_static_is_ok(a, ty, init, &bad);
}
static void cxx_gen_file_asm(cxx *c, const char *text);
static uint32_t cxx_gen_data_symbol(cxx *c, const char *name, uint32_t n);
static uint32_t cxx_gen_to_void_pointer(cxx *c, uint32_t n);
static uint32_t cxx_gen_runtime_call(cxx *c, uint32_t fs, const uint32_t *args, uint32_t nargs, uint32_t loc);

typedef struct cxx_prv {
	uint32_t node;		/* the prvalue as an expression: an lvalue naming the temporary */
	uint32_t expr;		/* the expression that constructs it (a call that takes the object's address first) */
	uint32_t call;		/* ... that call (0: built some other way, cannot be aimed elsewhere) */
	uint32_t tmp;		/* the temporary's symbol */
} cxx_prv;
static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);
static bool cxx_gen_is_reference(cxx *c, qty t);
static uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, qty to, uint32_t loc);
static uint32_t cxx_gen_base_object(cxx *c, uint32_t x, qty to, uint32_t loc);

/*
 * A type code is generated for so far, as a value (a parameter, a result, an operand): a scalar, a
 * reference to an object type that is, or a class that C's rules pass and copy as C++'s do (laid out,
 * POD for layout: no constructor, destructor or assignment of its own).
 */
VPRIV bool cxx_gen_type_ok(cxx *c, qty t)
{
	int k;
	bool pod, empty;

	if (t == CXX_TY_UNKNOWN)
		return false;
	k = AST_TYPE_KIND(c->ast, t);
	if (k == TY_PTR && AST_TYPE(c->ast, t)->ext) {
		/* a reference is an address, as a pointer is: a class it refers to need not be one code is built for
		   (std::ostream &, a virtual base's class); what is done through it is checked where it is done */
		qty b = AST_TYPE(c->ast, t)->base;

		if (b != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, b) && AST_TYPE_KIND(c->ast, b) != TY_ARRAY)
			return true;
		return cxx_gen_object_type_ok(c, b);
	}
	if (k == TY_STRUCT || k == TY_UNION)
		return cxx_class_layout_info(c, t, &pod, &empty) && pod &&	/* (an instance is completed here if it was not yet) */
		       (c->ast->tags[AST_TYPE(c->ast, t)->aux].flags & RF_COMPLETE);
	return (k >= TY_BOOL && k <= TY_UINT128) || k == TY_FLOAT || k == TY_DOUBLE || k == TY_LDOUBLE || k == TY_PTR || k == TY_ENUM;
}

/*
 * A class whose objects code is generated for: laid out, its bases and its members of class type such
 * classes too, its other members scalars or arrays of them.  It may declare constructors and a
 * destructor; passing it by value is then not built yet (cxx_gen_type_ok).
 */
static bool cxx_gen_class_ok2(cxx *c, qty t, bool objects);

__attribute__((unused)) VPRIV bool cxx_gen_class_ok(cxx *c, qty t)
{
	return cxx_gen_class_ok2(c, t, false);
}

/* objects: as an object in memory (a class with virtual functions too: never copied as a value here) */
static bool cxx_gen_class_ok2(cxx *c, qty t, bool objects)
{
	cc *a = c->ast;
	cxx_scope *cls;
	bool pod, empty;

#define CK(x, why) do { if (x) return false; } while (0)	/* (why: what VCC_CXX_GEN would say) */
	CK(t == CXX_TY_UNKNOWN || (AST_TYPE_KIND(a, t) != TY_STRUCT && AST_TYPE_KIND(a, t) != TY_UNION), "kind");
	CK(!cxx_class_layout_info(c, t, &pod, &empty), "layout");
	CK(!(a->tags[AST_TYPE(a, t)->aux].flags & RF_COMPLETE), "incomplete");
	if (pod)
		return true;
	cls = cxx_tag_scope_get(c, QUNQ(t));
	CK(!cls || cls->has_dependent_base, "dependent base");
	CK(cxx_class_polymorphic(c, t) && (!objects || !cxx_vtable_usable(c, cls)), "vtable");
	{
		qty bases[16];
		uint32_t nb = cxx_class_bases(c, t, bases, 16);

		for (uint32_t i = 0; i < nb; i++)
			if (!cxx_gen_class_ok2(c, bases[i], objects))
				return false;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
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
static bool cxx_gen_object_type_ok(cxx *c, qty t)
{
	while (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY && AST_TYPE(c->ast, t)->n)
		t = AST_TYPE(c->ast, t)->base;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != TY_ARRAY)
		return cxx_gen_class_ok2(c, t, true);
	return t != CXX_TY_UNKNOWN && !(AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext) && cxx_gen_type_ok(c, t);
}

static bool cxx_gen_is_reference(cxx *c, qty t)
{
	return t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext;
}

/* a temporary of the function body: a local of type t, declared in the block being built (0: no block) */
static uint32_t cxx_gen_temporary(cxx *c, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t s, n;

	if (!c->gen_block) {
		cxx_gen_fail(c);
		return 0;
	}
	s = cc_symbol_new(a, S_VAR, 0, QUNQ(t), loc);
	a->syms[s].sc = SC_AUTO;
	n = cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, 0);
	cc_id_list_append(a, (cc_id_list *)c->gen_block, n);
	return s;
}

/* does the class, or a class inside it, have a base class that takes space?  (C's view of the record has
   plain bytes there: its classification for registers would not be the ABI's) */
static bool cxx_gen_has_base_bytes(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (!cls || depth > 8)
		return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (!cxx_class_layout_info(c, bases[i], &pod, &empty) || !empty)
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
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
static int cxx_gen_trivial_abi(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	cxx_entity *d;
	qty bases[16];
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
		qty p;

		if (e->member_of != cls || e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, e->ty) != TY_FUNC)
			continue;
		ft = AST_TYPE(a, e->ty);
		if (!ft->n || ft->n > (uint32_t)e->ndefaults + 1)
			continue;
		p = a->params[ft->aux];
		if (p == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, p) != TY_PTR || !AST_TYPE(a, p)->ext || QUNQ(AST_TYPE(a, p)->base) != QUNQ(t))
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
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
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
static bool cxx_gen_by_address(cxx *c, qty t)
{
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return false;
	return cxx_gen_trivial_abi(c, t, 0) == 0;
}

/* a type as a parameter or a result: cxx_gen_type_ok's, and classes with constructors and destructors */
static bool cxx_gen_value_ok(cxx *c, qty t)
{
	cc *a = c->ast;
	int tr;

	if (cxx_gen_type_ok(c, t))
		return true;
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(a, t) || AST_TYPE_KIND(a, t) == TY_ARRAY || !cxx_gen_class_ok2(c, t, true))
		return false;
	tr = cxx_gen_trivial_abi(c, t, 0);
	if (tr == 1)
		return AST_TYPE(a, t)->size > 16 || !cxx_gen_has_base_bytes(c, t, 0);
	return tr == 0 && a->o->target == PP_LINUX_X86_64;	/* (the convention is written out for x86-64 only so far) */
}

/* ---- class prvalues and temporaries ---------------------------------------------------------- */

static cxx_prv *cxx_gen_prv_find(cxx *c, uint32_t node)
{
	for (uint32_t i = c->nprvs; node && i-- > 0 && c->nprvs - i <= 64;)
		if (c->prvs[i].node == node)
			return &c->prvs[i];
	return NULL;
}

static void cxx_gen_prv_add(cxx *c, uint32_t node, uint32_t expr, uint32_t call, uint32_t tmp)
{
	if (c->nprvs == c->cprvs)
		c->prvs = realloc(c->prvs, (c->cprvs = c->cprvs ? c->cprvs * 2 : 64) * sizeof *c->prvs);
	c->prvs[c->nprvs++] = (cxx_prv){ node, expr, call, tmp };
}

/* the prvalue is constructed in the object at `address` instead of its temporary (copy elision): the
   expression that does it (the temporary is left unused, and is not destroyed) */
static uint32_t cxx_gen_prv_aim(cxx *c, cxx_prv *p, uint32_t address)
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
static uint32_t cxx_gen_class_temp(cxx *c, qty t, uint32_t loc)
{
	cc *a = c->ast;
	bool needs = cxx_gen_needs_destruction(c, t, 0);
	cxx_entity *dtor = needs ? cxx_gen_effective_destructor(c, t, 0) : NULL;
	uint32_t s;

	if (needs && (!dtor || dtor->fn_deleted || c->gen_notemp || !cxx_gen_name(c, dtor) || !dtor->sym1 || (cxx_member_body_use(c, dtor), 0)))
		return 0;
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

static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

/* the statements' expressions (each an expression statement) as one expression, then `last` */
static uint32_t cxx_gen_chain(cxx *c, const cc_id_list *l, uint32_t last, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t x = 0;

	for (uint32_t i = 0; i < l->n; i++) {
		uint32_t e = AST_NODE_AT(a, l->v[i])->a;

		x = x ? cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, e)->ty, loc, x, e) : e;
	}
	if (!last)
		return x;
	return x ? cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, last)->ty, loc, x, last) : last;
}

/* T(args), T{args}: a temporary of class type, constructed (cxx_ctor_use was called for it) */
VPRIV void cxx_gen_construct_temp(cxx *c, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *picked = c->ctor_picked;
	bool found = c->ctor_found, none = c->ctor_none;
	cc_id_list l;
	uint32_t tmp, node, expr, call = 0, zero = 0;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	t = QUNQ(t);
	if (nargs == UINT32_MAX || nargs > 16 || !cxx_gen_class_ok2(c, t, true))
		return;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i])
			return;
	if (nargs == 1 && cxx_gen_prv_find(c, args[0]) && QUNQ(AST_NODE_AT(a, args[0])->ty) == t) {
		c->enode = args[0];	/* T(a prvalue of T): that object */
		return;
	}
	tmp = cxx_gen_class_temp(c, t, loc);
	if (!tmp)
		return;
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	cc_id_list_init(&l);
	if (!nargs && (!(found && picked && picked->member_of == cxx_tag_scope_get(c, t)) || picked->fn_defaulted)) {
		/* T() of a class without a user-provided default constructor: zeroes (then constructed, below, if
		   anything needs it) */
		uint32_t zargs[3], saved = c->enode;

		zargs[0] = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
		zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
		zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, t)->size, cc_type_basic(TY_ULONG), loc);
		c->enode = 0;
		cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
		zero = c->enode;
		c->enode = saved;
		if (!zero)
			return;
		if (cxx_gen_needs_construction(c, t, 0)) {
			c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
			cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
			zero = 0;
			if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, args, nargs, &l, loc)) {
				cxx_gen_fail(c);
				return;
			}
		}
	} else if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, args, nargs, &l, loc)) {
		cxx_gen_fail(c);	/* (the temporary is declared: nothing of this function is kept) */
		return;
	}
	expr = zero ? zero : cxx_gen_chain(c, &l, 0, loc);
	if (!zero && l.n == 1 && AST_NODE_AT(a, expr)->op == N_CALL && AST_NODE_AT(a, expr)->c)
		call = expr;
	node = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
	if (expr)
		node = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, node)->ty, loc, expr, node);
	node = cc_dereference_build(a, node, loc);
	if (call)
		cxx_gen_prv_add(c, node, expr, call, tmp);
	else
		cxx_gen_prv_add(c, node, 0, 0, tmp);
	c->enode = node;
}

/* an argument for a parameter of a class passed by address: the address of the temporary the argument is,
   or of a temporary copied from the argument (0: not built) */
static uint32_t cxx_gen_class_argument(cxx *c, uint32_t x, qty pt, uint32_t loc)
{
	cc *a = c->ast;
	qty t = QUNQ(pt), at[1];
	uint8_t lv[1];
	cxx_entity *call_picked = c->call_picked;
	uint8_t call_resolved = c->call_resolved, none_viable = c->none_viable, elval = c->elval, econst = c->econst, einvalid = c->einvalid;
	qty etype = c->etype;
	int64_t evalue = c->evalue;
	cxx_entity *picked;
	bool found, none;
	cc_id_list l;
	uint32_t tmp;

	if (!x)
		return 0;
	if (QUNQ(AST_NODE_AT(a, x)->ty) == t && cxx_gen_prv_find(c, x))
		return cc_address_of_build(a, x, loc);
	if (QUNQ(AST_NODE_AT(a, x)->ty) == t && !(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return 0;	/* (a prvalue of the class that is not a call's: not built yet) */
	/* the parameter copy-initialized from the argument: a copy, or a converting constructor from a scalar,
	   pointer or array (not an explicit one: json from 3 or "text" for operator=(basic_json)) */
	/* (a call's result is a prvalue though its node is an object here: CompatibleType&& must not deduce T&) */
	at[0] = AST_NODE_AT(a, x)->ty, lv[0] = (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 && !cxx_gen_prv_find(c, x);
	c->skip_explicit = QUNQ(at[0]) != t;
	cxx_ctor_use(c, t, at, lv, 1, false);
	c->skip_explicit = 0;
	picked = c->ctor_picked, found = c->ctor_found, none = c->ctor_none;
	c->call_picked = call_picked, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->etype = etype, c->elval = elval, c->econst = econst, c->evalue = evalue, c->einvalid = einvalid;
	tmp = cxx_gen_class_temp(c, t, loc);
	if (!tmp)
		return 0;
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	cc_id_list_init(&l);
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, &x, 1, &l, loc)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cxx_gen_chain(c, &l, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc), loc);
}

VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc);

/* an object of class type as a value of the scalar type `to`: its conversion function to exactly that type
   (operator bool for a condition); 0: none, or not built */
static uint32_t cxx_gen_convert_class(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	cxx_scope *cls;
	cxx_entity *f;
	char nm[64] = { 0 };
	uint32_t saved = c->enode, n;

	if (!x || !cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY ||
	    !(AST_NODE_AT(a, x)->flags & NF_LVALUE) || !(cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, x)->ty))))
		return 0;
	snprintf(nm, 32, "operator #%u", (unsigned)QUNQ(to));
	f = cxx_lookup_in(c, cls, vintern_cstr(nm), 0);
	if (!f || f->kind != CXX_ENTITY_VALUE || f->next || f->fn_deleted)
		return 0;
	if (f->late && !f->late_done)
		cxx_member_body_use(c, f);
	c->enode = 0;
	cxx_gen_call(c, f, x, false, NULL, 0, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}

/*
 * The value x for an object, parameter or result of type to: converted for a scalar, as it is for a class,
 * and for a reference the address of the lvalue it binds to (for a const T& or T&& bound to a value: of a
 * temporary holding it).  0: not built.
 */
/* a nullptr_t object holding pointer value x (a temporary, an lvalue): *(void **)&tmp = x, tmp */
static uint32_t cxx_gen_nullptr_object(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t tmp = cxx_gen_temporary(c, c->nullptr_ty, loc), tv, slot, asg;

	if (!tmp)
		return 0;
	tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
	AST_NODE_AT(a, tv)->a = tmp, AST_NODE_AT(a, tv)->flags = NF_LVALUE;
	slot = cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, tv, loc), cc_type_pointer(a, vp), CK_NOP), loc);
	asg = cc_assignment_build(a, 0, slot, cc_conversion_apply(a, cc_node_rvalue(a, x), vp), loc);
	tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
	AST_NODE_AT(a, tv)->a = tmp, AST_NODE_AT(a, tv)->flags = NF_LVALUE;
	x = cc_node_new_operands(a, N_COMMA, a->syms[tmp].ty, loc, asg, tv);
	AST_NODE_AT(a, x)->flags |= NF_LVALUE;
	return x;
}

static uint32_t cxx_gen_bind(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;

	if (!x)
		return 0;
	if (c->nullptr_ty && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR && !AST_TYPE(a, AST_NODE_AT(a, x)->ty)->ext &&
	    QUNQ(cxx_gen_is_reference(c, to) ? AST_TYPE(a, to)->base : to) == c->nullptr_ty) {
		/* nullptr (a void * here) to a std::nullptr_t parameter: an object of that type */
		x = cxx_gen_nullptr_object(c, x, loc);
		if (!x)
			return 0;
		if (!cxx_gen_is_reference(c, to))
			return cc_node_rvalue(a, x);
		return cc_cast_insert(a, cc_address_of_build(a, x, loc), QUNQ(to), CK_NOP);
	}
	if (c->nullptr_ty && QUNQ(AST_NODE_AT(a, x)->ty) == c->nullptr_ty && (AST_NODE_AT(a, x)->flags & NF_LVALUE) &&
	    AST_TYPE_KIND(a, to) == TY_PTR && !cxx_gen_is_reference(c, to)) {
		/* a std::nullptr_t object to a pointer: the void * it holds */
		qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

		x = cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, x, loc), cc_type_pointer(a, vp), CK_NOP), loc);
		return cc_conversion_apply(a, cc_node_rvalue(a, x), QUNQ(to));
	}
	if (cxx_gen_is_reference(c, to)) {
		qty rt = AST_TYPE(a, to)->base;

		if (cc_type_is_record(a, rt) && AST_TYPE_KIND(a, rt) != TY_ARRAY &&
		    (!cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY)) {
			/* a reference to a class bound to a value of another type ("text" for a const std::string &): a
			   temporary made by the class's converting constructor */
			qty at[1] = { AST_NODE_AT(a, x)->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 };
			uint32_t saved = c->enode, tmp;

			if (AST_TYPE(a, to)->ext == 1 && !(QQ(rt) & Q_CONST))
				return 0;	/* (T& does not bind a temporary) */
			if (AST_TYPE_KIND(a, at[0]) == TY_ARRAY)	/* ("text": a pointer, as for a call) */
				at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
			cxx_ctor_use(c, QUNQ(rt), at, lv, 1, false);
			if (!c->ctor_found || !c->ctor_picked || c->ctor_picked->fn_explicit)
				return 0;
			c->enode = 0;
			cxx_gen_construct_temp(c, QUNQ(rt), &x, 1, loc);
			tmp = c->enode;
			c->enode = saved;
			if (!tmp || !(AST_NODE_AT(a, tmp)->flags & NF_LVALUE))
				return 0;
			return cc_cast_insert(a, cc_address_of_build(a, tmp, loc), QUNQ(to), CK_NOP);
		}
		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && cc_type_is_record(a, rt) && cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
		    QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt)) {
			/* a reference to a base class bound to a derived object: its base subobject; to another class:
			   the object its conversion function to that class gives (operator Result()) */
			qty from = QUNQ(AST_NODE_AT(a, x)->ty);
			uint64_t off;

			if (cxx_class_base_offset(c, from, QUNQ(rt), &off, 0))
				x = cxx_gen_base_object(c, x, QUNQ(rt), loc);
			else
				x = cxx_gen_convert_class(c, x, QUNQ(rt), loc);
			if (!x)
				return 0;
		}
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE) || (cxx_gen_type_ok(c, rt) && !cc_type_is_record(a, rt) &&
								  QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt))) {
			/* a value (or an lvalue of another type, converted): a temporary */
			uint32_t tmp, tv, asg;

			if (AST_TYPE(a, to)->ext == 1 && !(QQ(rt) & Q_CONST))
				return 0;	/* (T& does not bind a value) */
			if (!cxx_gen_object_type_ok(c, rt) || AST_TYPE_KIND(a, rt) == TY_ARRAY)
				return 0;
			if (cc_type_is_record(a, rt) != cc_type_is_record(a, AST_NODE_AT(a, x)->ty) ||
			    (cc_type_is_record(a, rt) && QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt)))
				return 0;	/* (a class from another type: a converting constructor or conversion function, not built yet) */
			tmp = cxx_gen_temporary(c, rt, loc);
			if (!tmp)
				return 0;
			tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
			AST_NODE_AT(a, tv)->a = tmp;
			AST_NODE_AT(a, tv)->flags = NF_LVALUE;
			asg = cc_assignment_build(a, 0, tv, cc_type_is_record(a, rt) ? x : cc_conversion_apply(a, cc_conversion_decay(a, x), QUNQ(rt)), loc);
			tv = cc_node_new(a, N_VAR, a->syms[tmp].ty, loc);
			AST_NODE_AT(a, tv)->a = tmp;
			AST_NODE_AT(a, tv)->flags = NF_LVALUE;
			x = cc_node_new_operands(a, N_COMMA, cc_type_pointer(a, a->syms[tmp].ty), loc, asg, cc_address_of_build(a, tv, loc));
		} else
			x = cc_address_of_build(a, x, loc);
		return cc_cast_insert(a, x, QUNQ(to), CK_NOP);
	}
	if (cc_type_is_record(a, to)) {
		if (QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(to))
			return cc_node_rvalue(a, x);
		if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY) {
			/* another class's object: its conversion function to this class (strong_ordering to partial_ordering) */
			uint32_t v = cxx_gen_convert_class(c, x, QUNQ(to), loc);

			return v && QUNQ(AST_NODE_AT(a, v)->ty) == QUNQ(to) ? cc_node_rvalue(a, v) : 0;
		}
		{
			/* a value of another type: the class's converting constructor (0 for _CmpUnspecifiedParam) */
			qty at[1] = { AST_NODE_AT(a, x)->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, x)->flags & NF_LVALUE) != 0 };
			uint32_t saved = c->enode, tmp;

			if (AST_TYPE_KIND(a, at[0]) == TY_ARRAY)
				at[0] = cc_type_pointer(a, AST_TYPE(a, at[0])->base), lv[0] = 0;
			cxx_ctor_use(c, QUNQ(to), at, lv, 1, false);
			if (!c->ctor_found || !c->ctor_picked || c->ctor_picked->fn_explicit)
				return 0;
			c->enode = 0;
			cxx_gen_construct_temp(c, QUNQ(to), &x, 1, loc);
			tmp = c->enode;
			c->enode = saved;
			return tmp ? cc_node_rvalue(a, tmp) : 0;
		}
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_ARRAY) {
		x = cxx_gen_convert_class(c, x, to, loc);	/* a class to a scalar: its conversion function to that type */
		if (!x)
			return 0;
	}
	x = cc_conversion_decay(a, x);
	if (AST_TYPE_KIND(a, to) == TY_PTR && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR) {
		/* Derived* to Base*: the pointer moved to the base subobject */
		qty tb = AST_TYPE(a, to)->base, fb = AST_TYPE(a, AST_NODE_AT(a, x)->ty)->base;

		if (cc_type_is_record(a, tb) && cc_type_is_record(a, fb) && QUNQ(tb) != QUNQ(fb)) {
			x = cxx_gen_base_pointer(c, x, tb, loc);
			return x ? cc_cast_insert(a, x, QUNQ(to), CK_NOP) : 0;
		}
	}
	return cc_conversion_apply(a, x, QUNQ(to));
}

/* a reference as an expression: the lvalue it refers to */
static uint32_t cxx_gen_referent(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty t = AST_NODE_AT(a, x)->ty;

	x = cc_node_rvalue(a, x);
	x = cc_cast_insert(a, x, cc_type_pointer(a, AST_TYPE(a, t)->base), CK_NOP);	/* (a plain pointer to the referent) */
	return cc_dereference_build(a, x, loc);
}

/*
 * A pointer to an object of class type as a pointer to its base class subobject (to: the base class, with
 * the cv of the pointee): the pointer moved by the base's offset.  0: not a base of it.
 */
static uint32_t cxx_gen_base_pointer(cxx *c, uint32_t p, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty pt = AST_NODE_AT(a, p)->ty, from;
	uint64_t off;

	if (AST_TYPE_KIND(a, pt) != TY_PTR)
		return 0;
	from = AST_TYPE(a, pt)->base;
	if (QUNQ(from) == QUNQ(to))
		return cc_cast_insert(a, p, cc_type_pointer(a, to), CK_NOP);
	if (!cxx_class_base_offset(c, QUNQ(from), QUNQ(to), &off, 0))
		return 0;
	if (off) {
		p = cc_cast_insert(a, p, cc_type_pointer(a, cc_type_basic(TY_CHAR)), CK_NOP);
		p = cc_binary_build(a, LEX_TOKEN_PLUS, p, cc_node_integer_new(a, (i128)off, cc_type_basic(TY_LONG), loc), loc);
	}
	return cc_cast_insert(a, p, cc_type_pointer(a, to), CK_NOP);
}

/* an lvalue of class type as its base class subobject (0: not a base of it) */
static uint32_t cxx_gen_base_object(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty xt = AST_NODE_AT(a, x)->ty;
	uint32_t p;

	if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return 0;
	p = cxx_gen_base_pointer(c, cc_address_of_build(a, x, loc), QT(QI(to), QQ(to) | QQ(xt)), loc);
	return p ? cc_dereference_build(a, p, loc) : 0;
}

/* the member `name` of the object x: a field of its class, or of a base class (the subobject first) */
static uint32_t cxx_gen_member_of(cxx *c, uint32_t x, uint32_t name, uint32_t loc, int depth)
{
	cc *a = c->ast;
	qty bt = AST_NODE_AT(a, x)->ty, bases[16];
	uint64_t bits;
	uint32_t nb;

	if (!cc_type_is_record(a, bt) || !(a->tags[AST_TYPE(a, bt)->aux].flags & RF_COMPLETE) || depth > 8)
		return 0;
	if (cxx_class_field_offset(c, bt, name, &bits, 0))
		return member(a, x, name, loc);
	nb = cxx_class_bases(c, bt, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		uint32_t sub = cxx_gen_base_object(c, x, bases[i], loc), m;

		if (sub && (m = cxx_gen_member_of(c, sub, name, loc, depth + 1)))
			return m;
	}
	return 0;
}

/* base.name or base->name, a data member */
VPRIV void cxx_gen_member(cxx *c, uint32_t base, bool arrow, uint32_t name, uint32_t loc)
{
	cc *a = c->ast;
	qty bt;

	c->enode = 0;
	if (!cxx_gen_on(c) || !base)
		return;
	if (arrow)
		base = cc_dereference_build(a, cc_conversion_decay(a, base), loc);
	(void)bt;
	c->enode = cxx_gen_member_of(c, base, name, loc, 0);	/* (0: a static member, or one of a class not laid out) */
	if (c->enode && (QQ(AST_NODE_AT(a, c->enode)->ty) & Q_CONST) && cc_type_is_record(a, AST_NODE_AT(a, base)->ty)) {
		/* a mutable member of a const object: not const */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, base)->ty));
		cxx_entity *fe = cls ? cxx_lookup_in(c, cls, name, 0) : NULL;

		while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
			fe = fe->next;
		if (fe && fe->is_mutable && fe->ty != CXX_TY_UNKNOWN && !(QQ(fe->ty) & Q_CONST)) {
			qty pt = cc_type_pointer(a, QUNQ(AST_NODE_AT(a, c->enode)->ty));
			uint32_t addr = cc_address_of_build(a, c->enode, loc);

			c->enode = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
	}
	if (c->enode && cxx_gen_is_reference(c, AST_NODE_AT(a, c->enode)->ty))
		c->enode = cxx_gen_referent(c, c->enode, loc);
}

/* the symbol a complete object's destruction calls: D2 for an inline destructor (a class without virtual
   bases: D1 does the same, and clang defines only D2 where it is inline, std::map's ~map), D1 otherwise */
static uint32_t cxx_gen_dtor_sym(const cxx_entity *d)
{
	bool inl = d->fn_inline || (d->member_of && d->member_of->owner && d->member_of->owner->instance);

	return inl && d->sym ? d->sym : d->sym1;
}

/* is the scope in an unnamed namespace (itself one, or inside one: its names have internal linkage) */
VPRIV bool cxx_scope_in_unnamed_namespace(cxx *c, const cxx_scope *s)
{
	for (; s; s = s->parent)
		if (s->kind == CXX_SCOPE_NAMESPACE && !s->owner && s != c->global)
			return true;
	return false;
}

/*
 * The function's symbol name: C++'s mangled name (mangle.c), or the plain name for main and extern "C".
 * False: a name not mangled yet.
 */
/* is f a member function called on an object (not static)? */
static bool cxx_gen_has_this(const cxx_entity *f)
{
	return f->member_of && f->member_of->kind == CXX_SCOPE_CLASS && !f->fn_static;
}

/* the class a member function belongs to (its this is a pointer to it, const for a const member) */
static qty cxx_gen_this_type(cxx *c, const cxx_entity *f)
{
	qty ct = f->member_of->owner ? f->member_of->owner->ty : CXX_TY_UNKNOWN;

	if (ct == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
	return cc_type_pointer(c->ast, f->fn_const ? QT(QI(ct), QQ(ct) | Q_CONST) : ct);
}

/* a string of the AST (an assembler name) */
static uint32_t cxx_gen_text(cxx *c, const char *text)
{
	cc *a = c->ast;
	size_t n = strlen(text);
	char *keep = cc_heap_allocate(a, n + 1);
	uint32_t si = a->nstr++;

	memcpy(keep, text, n + 1);
	a->strs[si] = (cstr){ keep, (uint32_t)n + 1, 1, LEX_TOKEN_STRING };
	return si;
}

static bool cxx_gen_is_special(const cxx_entity *f)
{
	return vintern_ents[f->name].len && vintern_ents[f->name].s[0] == ' ';	/* a constructor or destructor */
}

/*
 * The function's type as the ABI calls it: this first for a member function; a class that is not trivial
 * for calls is passed by the address of a temporary, and returned in an object whose address comes first
 * of all (x86-64's convention written out: the pointer in rdi, returned in rax).
 */
static qty cxx_gen_abi_type(cxx *c, const cxx_entity *f)
{
	cc *a = c->ast;
	const ctype *ft = AST_TYPE(a, f->ty);
	qty ps[34], ret = ft->base;
	uint32_t np = 0;
	bool any = false;

	if (ft->n > 31)
		return CXX_TY_UNKNOWN;
	if (cxx_gen_by_address(c, ret)) {
		ps[np++] = cc_type_pointer(a, QUNQ(ret));
		ret = ps[0], any = true;
	}
	if (cxx_gen_has_this(f)) {
		qty tt = cxx_gen_this_type(c, f);

		if (tt == CXX_TY_UNKNOWN || (cxx_class_polymorphic(c, AST_TYPE(a, tt)->base) && !cxx_vtable_usable(c, f->member_of)))
			return CXX_TY_UNKNOWN;	/* (a class with virtual functions whose vtable is not built yet) */
		ps[np++] = tt, any = true;
	}
	for (uint32_t i = 0; i < ft->n; i++) {
		qty p = a->params[ft->aux + i];

		if (cxx_gen_by_address(c, p))
			p = cc_type_pointer(a, QUNQ(p)), any = true;
		ps[np++] = p;
	}
	if (!any)
		return f->ty;
	ft = AST_TYPE(a, f->ty);
	return cxx_type_function(c, ret, ps, np, (ft->flags & TF_VARIADIC) != 0);
}

/* has a function by this mangled name been begun already (else it is entered): a hash set, open addressing
   (a list searched with strcmp was 9 % of a json test's compile time) */
static bool cxx_gen_name_defined(cxx *c, const char *nm)
{
	uint32_t h = 2166136261u, mask, i;

	for (const char *p = nm; *p; p++)
		h = (h ^ (unsigned char)*p) * 16777619u;
	if (c->ngen_names * 2 >= c->cgen_names) {
		uint32_t ocap = c->cgen_names, ncap = ocap ? ocap * 2 : 1024;
		char **old = c->gen_names;

		c->gen_names = calloc(ncap, sizeof *c->gen_names);
		c->cgen_names = ncap, c->ngen_names = 0;
		for (uint32_t k = 0; k < ocap; k++)
			if (old[k])
				cxx_gen_name_defined(c, old[k]);
		free(old);
	}
	mask = c->cgen_names - 1;
	for (i = h & mask; c->gen_names[i]; i = (i + 1) & mask)
		if (!strcmp(c->gen_names[i], nm))
			return true;
	c->gen_names[i] = (char *)nm;
	c->ngen_names++;
	return false;
}

/* the function symbol with this assembler name: the one made first (0: none yet; then `s` is entered, if not 0) */
static uint32_t cxx_gen_symbol_named(cxx *c, const char *name, uint32_t s)
{
	uint32_t h = 2166136261u, mask, i;

	for (const char *p = name; *p; p++)
		h = (h ^ (unsigned char)*p) * 16777619u;
	if (c->nsym_names * 2 >= c->csym_names) {
		uint32_t ncap = c->csym_names ? c->csym_names * 2 : 1024;
		void *old = c->sym_names;
		uint32_t ocap = c->csym_names;

		c->sym_names = calloc(ncap, sizeof *c->sym_names);
		c->csym_names = ncap, c->nsym_names = 0;
		for (uint32_t k = 0; k < ocap; k++) {
			struct { const char *name; uint32_t sym; } *o = old;

			if (o[k].name)
				cxx_gen_symbol_named(c, o[k].name, o[k].sym);
		}
		free(old);
	}
	mask = c->csym_names - 1;
	for (i = h & mask; c->sym_names[i].name; i = (i + 1) & mask)
		if (!strcmp(c->sym_names[i].name, name))
			return c->sym_names[i].sym;
	if (s) {
		c->sym_names[i].name = name, c->sym_names[i].sym = s;
		c->nsym_names++;
	}
	return 0;
}

/* a variable's symbol named as C++ mangles it (a namespace's variable, a static data member; one at the
   global scope or extern "C" keeps its plain name).  False: not mangled (its symbol is dropped: a use of
   it builds no code, rather than code under the wrong name). */
VPRIV bool cxx_gen_variable_name(cxx *c, cxx_entity *v, bool internal)
{
	cc *a = c->ast;
	char name[1024];
	attrs at;
	cxx_scope *home = v->home;

	if (!v->sym || v->c_linkage || !home || home == c->global)
		return true;
	if (!cxx_mangle_variable(c, v, home, internal, name, sizeof name)) {
		v->sym = 0;
		return false;
	}
	{
		uint32_t have = cxx_gen_symbol_named(c, name, 0);

		if (have) {	/* (another declaration of the same variable) */
			v->sym = have;
			return true;
		}
	}
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[v->sym], &at);
	cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, v->sym);
	return true;
}

/*
 * The function's symbols and their names: C++'s mangled name (mangle.c), or the plain name for main and
 * extern "C".  A member function's symbol is made here (its type with this first); an inline function's is
 * weak (every TU that uses it defines it: no COMDAT group yet).  A constructor or destructor has two: sym
 * the base-object function (C2, D2: the one with the body) and sym1 the complete-object one (C1, D1: what
 * is called; an alias of the other where the body is).  False: a name not mangled yet.
 */
static bool cxx_gen_name(cxx *c, cxx_entity *f)
{
	cc *a = c->ast;
	const vident *v = &vintern_ents[f->name];
	char name[2048];
	attrs at;
	cxx_scope *home = f->member_of ? f->member_of : f->home;
	bool special = cxx_gen_is_special(f);

	if (f->mangled || f->c_linkage)
		return true;
	if (!home || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC)
		return false;
	if (home == c->global && v->len == 4 && !memcmp(v->s, "main", 4)) {
		f->mangled = 1;
		return true;
	}
	c->mangle_guess = 0;
	if (!cxx_mangle_function(c, f, home, f->ty, f->member_of && f->fn_const, special ? 2 : 0, name, sizeof name))
		return false;
	f->name_guess = c->mangle_guess;
	if (f->name_guess && getenv("VCC_CXX_MANGLE_WHY"))
		fprintf(stderr, "name guessed: %s\n", name);	/* (VCC_CXX_MANGLE_WHY: the names only vcc gives) */
	{
		/* another entity for the same function (the same mangled name) has the symbol already */
		uint32_t have = cxx_gen_symbol_named(c, name, 0);

		if (have) {
			f->sym = have;
			f->sym1 = special ? have : 0;
			if (special) {
				/* (its other names: looked up the same way below, or the one symbol for an inline one) */
				char n1[2048];

				if (cxx_mangle_function(c, f, home, f->ty, false, 1, n1, sizeof n1) && cxx_gen_symbol_named(c, n1, 0))
					f->sym1 = cxx_gen_symbol_named(c, n1, 0);
				if (v->s[1] != 'c' && cxx_mangle_function(c, f, home, f->ty, false, 3, n1, sizeof n1))
					f->sym0 = cxx_gen_symbol_named(c, n1, 0);
			}
			f->mangled = 1;
			return true;
		}
	}
	if (!f->sym || cxx_gen_abi_type(c, f) != f->ty) {
		qty st = cxx_gen_abi_type(c, f);

		if (st == CXX_TY_UNKNOWN)
			return false;
		f->sym = cc_symbol_new(a, S_FUNC, f->name, st, 0);
		if ((!f->fn_internal || f->member_of) && !cxx_scope_in_unnamed_namespace(c, home))
			a->syms[f->sym].flags |= SF_GLOBAL;	/* (in an unnamed namespace, a class's member too: internal) */
		a->syms[f->sym].depth = 0;	/* (made while a body is being built: a file-scope symbol all the same) */
	}
	if (cxx_scope_in_unnamed_namespace(c, home)) {
		a->syms[f->sym].flags &= ~SF_GLOBAL;	/* (internal linkage, however the symbol was made) */
		a->syms[f->sym].sc = SC_STATIC;
	}
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym);
	if (special && f->fn_inline)
		f->sym1 = f->sym;	/* (an inline one: every TU that uses it has its own, so the one function, C2 or D2, does for both names) */
	if (special && !f->fn_inline) {
		if (!cxx_mangle_function(c, f, home, f->ty, false, 1, name, sizeof name))
			return false;
		f->sym1 = cc_symbol_new(a, S_FUNC, f->name, a->syms[f->sym].ty, 0);
		a->syms[f->sym1].flags |= SF_GLOBAL;
		a->syms[f->sym1].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
		cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym1);
	}
	if (special && f->fn_virtual && v->s[1] != 'c') {
		/* a virtual destructor: the deleting destructor too */
		if (!cxx_mangle_function(c, f, home, f->ty, false, 3, name, sizeof name))
			return false;
		f->sym0 = cc_symbol_new(a, S_FUNC, f->name, a->syms[f->sym].ty, 0);
		a->syms[f->sym0].flags |= SF_GLOBAL;
		a->syms[f->sym0].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym0], &at);
		cxx_gen_symbol_named(c, a->strs[at.asmname - 1].s, f->sym0);
	}
	f->mangled = 1;
	return true;
}

/* this, in a member function's body */
VPRIV void cxx_gen_this(cxx *c, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !c->gen_this)
		return;
	if (c->gen_lambda) {
		/* in a lambda's body: the enclosing function's this, captured */
		int k = cxx_gen_capture(c, c->gen_lambda, NULL, loc);

		if (k < 0) {
			cxx_gen_fail(c);
			return;
		}
		c->enode = cc_node_rvalue(c->ast, cxx_gen_var_node(c, c->gen_lambda->cap[k].local, loc));
		return;
	}
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[c->gen_this].ty, loc);
	AST_NODE_AT(c->ast, n)->a = c->gen_this;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	c->enode = cc_node_rvalue(c->ast, n);
}

/* the member function of class cls named name with these parameter types and constness (an out-of-class definition's) */
VPRIV cxx_entity *cxx_member_function_find(cxx *c, cxx_scope *cls, uint32_t name, const cxx_param *ps, uint32_t nps, bool is_const)
{
	cc *a = c->ast;

	for (cxx_entity *e = cxx_lookup_in(c, cls, name, 0); e; e = e->next) {
		const ctype *ft;
		bool same = true;

		if (e->kind != CXX_ENTITY_VALUE || e->member_of != cls || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, e->ty) != TY_FUNC)
			continue;
		ft = AST_TYPE(a, e->ty);
		if (ft->n != nps || (bool)e->fn_const != is_const)
			continue;
		for (uint32_t i = 0; i < nps && same; i++)
			same = ps[i].ty != CXX_TY_UNKNOWN && QUNQ(a->params[ft->aux + i]) == QUNQ(ps[i].ty);
		if (same)
			return e;
	}
	return NULL;
}

VPRIV void cxx_gen_integer(cxx *c, int64_t v, qty t, uint32_t loc)
{
	if (cxx_gen_on(c) && cxx_gen_type_ok(c, t))
		c->enode = cc_node_integer_new(c->ast, (i128)v, t, loc);
}

/* a floating literal (its spelling: decimal or hexadecimal, digit separators, a suffix f, l) */
VPRIV void cxx_gen_floating(cxx *c, const char *text, size_t len, qty t, uint32_t loc)
{
	char buf[128];
	size_t n = 0;
	int k = t != CXX_TY_UNKNOWN ? AST_TYPE_KIND(c->ast, t) : -1;

	if (!cxx_gen_on(c) || (k != TY_FLOAT && k != TY_DOUBLE && k != TY_LDOUBLE) || len >= sizeof buf)
		return;
	for (size_t i = 0; i < len; i++)
		if (text[i] != '\'')
			buf[n++] = text[i];
	buf[n] = 0;
	if (cc_type_is_float128(c->ast, t)) {
		/* a binary128 long double (Arm Linux): parsed to its own precision, as C does */
		size_t m = n;

		while (m && (buf[m - 1] == 'l' || buf[m - 1] == 'L'))
			m--;
		c->enode = cc_node_float128_new(c->ast, vsoftfloat_parse_with_precision(buf, m, cc_float_precision_bits(c->ast, t)), t, loc);
		return;
	}
	c->enode = cc_node_float_new(c->ast, k == TY_FLOAT ? (double)strtof(buf, NULL) : strtod(buf, NULL), t, loc);	/* (strto* stop at the suffix) */
}

/* ---- lambdas ------------------------------------------------------------------------------- */

/*
 * The capture of v (NULL: of this) in the lambda being built: found, or made if the capture list allows
 * it.  Its index, -1 if it is not allowed or not built (a class copied by value that is not trivial).
 */
static int cxx_gen_capture(cxx *c, cxx_gen_lambda *l, const cxx_entity *v, uint32_t loc)
{
	cc *a = c->ast;
	bool by_ref = l->def == 1, allowed = l->def != 0;
	qty t, lt;
	uint32_t s;

	for (uint32_t k = 0; k < l->n; k++)
		if (v ? (!l->cap[k].is_this && l->cap[k].sym == v->sym) : l->cap[k].is_this)
			return (int)k;
	if (l->n == 32)
		return -1;
	if (!v) {
		if (!allowed && !l->ex_this)
			return -1;
		if (c->this_type == CXX_TY_UNKNOWN || !c->this_type)
			return -1;
		t = cc_type_pointer(a, c->this_const ? QT(QI(c->this_type), QQ(c->this_type) | Q_CONST) : c->this_type);
		lt = t;
	} else {
		for (uint32_t i = 0; i < l->nex; i++)
			if (l->ex[i].name == v->name)
				allowed = true, by_ref = l->ex[i].by_ref;
		if (!allowed)
			return -1;
		t = a->syms[v->sym].ty;
		if (cxx_gen_is_reference(c, t))
			t = AST_TYPE(a, t)->base;
		if (!by_ref && (AST_TYPE_KIND(a, t) == TY_ARRAY || !(cxx_gen_type_ok(c, QUNQ(t)) || cxx_gen_trivial_abi(c, QUNQ(t), 0) == 1)))
			return -1;	/* (a copy of an array, or of a class with a copy constructor: not built yet) */
		lt = cc_type_pointer(a, t);
		t = by_ref ? lt : QUNQ(t);
	}
	s = cc_symbol_new(a, S_VAR, 0, lt, loc);
	a->syms[s].sc = SC_AUTO;
	l->cap[l->n].sym = v ? v->sym : 0, l->cap[l->n].e = v, l->cap[l->n].by_ref = by_ref, l->cap[l->n].is_this = !v;
	l->cap[l->n].local = s, l->cap[l->n].fty = t;
	return (int)l->n++;
}

static uint32_t cxx_gen_capture_name(uint32_t k)
{
	char nm[64] = { 0 };

	snprintf(nm, 32, "__c%u", k);
	return vintern_cstr(nm);
}

/* a closure type's operator(): its symbol, a local one with a name of the TU's own */
VPRIV bool cxx_gen_lambda_symbol(cxx *c, cxx_entity *call, uint32_t serial)
{
	cc *a = c->ast;
	char nm[64];
	attrs at;
	qty st = cxx_gen_abi_type(c, call);

	if (st == CXX_TY_UNKNOWN)
		return false;
	call->sym = cc_symbol_new(a, S_FUNC, call->name, st, 0);
	a->syms[call->sym].depth = 0;
	a->syms[call->sym].sc = SC_STATIC;	/* (internal linkage) */
	snprintf(nm, sizeof nm, " __vcc_lambda_%u", serial);
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, nm + 1) + 1;
	cc_attribute_apply_symbol(a, &a->syms[call->sym], &at);
	call->mangled = 1, call->fn_internal = 1;
	return true;
}

/*
 * The lambda's body is built (still inside its operator()): the closure type gets its members, one per
 * capture, and the body is preceded by the pointers to the captured objects, read from the closure.
 */
VPRIV bool cxx_gen_lambda_finish(cxx *c, cxx_gen_lambda *l, cxx_entity *owner, uint32_t *body, uint32_t loc)
{
	cc *a = c->ast;
	uint8_t path_valid = c->name_path_valid;
	cc_id_list list;

	if (l->bad || !*body || !c->gen_this)
		return false;
	c->name_path_valid = 0;
	cxx_class_begin(c, owner, owner->name, loc, false, -1, false);
	for (uint32_t k = 0; k < l->n; k++)
		cxx_class_field(c, cxx_gen_capture_name(k), l->cap[k].fty, loc, -1);
	cxx_class_end(c);
	c->name_path_valid = path_valid;
	if (!(a->tags[AST_TYPE(a, owner->ty)->aux].flags & RF_COMPLETE))
		return false;
	if (!l->n)
		return true;
	cc_id_list_init(&list);
	for (uint32_t k = 0; k < l->n; k++) {
		uint32_t self = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_this, loc)), loc);
		uint32_t fld = member(a, self, cxx_gen_capture_name(k), loc), v;
		qty lt = a->syms[l->cap[k].local].ty;

		v = l->cap[k].is_this || l->cap[k].by_ref ? cc_node_rvalue(a, fld) : cc_address_of_build(a, fld, loc);
		v = cc_cast_insert(a, v, lt, cc_conversion_cast_kind(a, AST_NODE_AT(a, v)->ty, lt, v));
		cc_id_list_append(a, &list, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, l->cap[k].local, 0));
		cc_id_list_append(a, &list, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, l->cap[k].local, loc), v, loc), 0, 0));
	}
	cc_id_list_append(a, &list, *body);
	*body = cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &list), list.n);
	return true;
}

/* the lambda expression's value, where it stands: a temporary of the closure type holding its captures */
VPRIV void cxx_gen_lambda_object(cxx *c, cxx_gen_lambda *l, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t tmp, expr = 0, node;

	c->enode = 0;
	if (!cxx_gen_on(c))
		return;
	tmp = cxx_gen_temporary(c, t, loc);
	if (!tmp)
		return;
	for (uint32_t k = 0; k < l->n; k++) {
		uint32_t fld, v, asg;
		qty ft = l->cap[k].fty;

		c->enode = 0;
		if (l->cap[k].is_this)
			cxx_gen_this(c, loc);
		else if (l->cap[k].sym && !c->gen_lambda) {
			/* the variable captured, by its symbol (a pack's element: its entity names another by now) */
			uint32_t n = cxx_gen_var_node(c, l->cap[k].sym, loc);

			c->enode = cxx_gen_is_reference(c, c->ast->syms[l->cap[k].sym].ty) ? cxx_gen_referent(c, n, loc) : n;
		} else
			cxx_gen_variable(c, l->cap[k].e, loc);
		v = c->enode;
		c->enode = 0;
		if (!v) {
			cxx_gen_fail(c);
			return;
		}
		if (l->cap[k].by_ref && !l->cap[k].is_this) {
			if (!(AST_NODE_AT(a, v)->flags & NF_LVALUE)) {
				cxx_gen_fail(c);
				return;
			}
			v = cc_address_of_build(a, v, loc);
		}
		if (l->cap[k].by_ref || l->cap[k].is_this)
			v = cc_cast_insert(a, v, ft, cc_conversion_cast_kind(a, AST_NODE_AT(a, v)->ty, ft, v));
		else if (cc_type_is_record(a, ft))
			v = cc_node_rvalue(a, v);
		else
			v = cc_conversion_apply(a, cc_conversion_decay(a, v), ft);
		fld = member(a, cxx_gen_var_node(c, tmp, loc), cxx_gen_capture_name(k), loc);
		asg = cc_assignment_build(a, 0, fld, v, loc);
		expr = expr ? cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, asg)->ty, loc, expr, asg) : asg;
	}
	if (expr) {
		node = cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc);
		node = cc_dereference_build(a, cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, node)->ty, loc, expr, node), loc);
	} else
		node = cxx_gen_var_node(c, tmp, loc);
	c->enode = node;
}

/* a variable (a parameter, a local, one at namespace scope) by name */
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !v->sym || !(cxx_gen_object_type_ok(c, v->ty) || cxx_gen_type_ok(c, v->ty)))
		return;
	if (v->has_value && v->ty != CXX_TY_UNKNOWN && (QQ(v->ty) & Q_CONST) && !c->ast->syms[v->sym].depth &&
	    !(c->ast->syms[v->sym].flags & (SF_DEFINED | SF_TENTATIVE)) &&
	    ((AST_TYPE_KIND(c->ast, v->ty) >= TY_BOOL && AST_TYPE_KIND(c->ast, v->ty) <= TY_ULLONG) || AST_TYPE_KIND(c->ast, v->ty) == TY_ENUM)) {
		/* a constant at namespace scope (constexpr, a const integer) with no object in this TU: its value */
		c->enode = cc_node_integer_new(c->ast, (i128)v->value, QUNQ(v->ty), loc);
		return;
	}
	if (c->gen_lambda && v->sym < c->gen_lambda->floor && c->ast->syms[v->sym].sc == SC_AUTO) {
		/* in a lambda's body: a variable of an enclosing function is a capture */
		int k = cxx_gen_capture(c, c->gen_lambda, v, loc);

		if (k < 0) {
			cxx_gen_fail(c);
			return;
		}
		c->enode = cc_dereference_build(c->ast, cc_node_rvalue(c->ast, cxx_gen_var_node(c, c->gen_lambda->cap[k].local, loc)), loc);
		return;
	}
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[v->sym].ty, loc);
	AST_NODE_AT(c->ast, n)->a = v->sym;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	if (cxx_gen_is_reference(c, c->ast->syms[v->sym].ty))
		n = cxx_gen_referent(c, n, loc);	/* (a reference names what it refers to) */
	c->enode = n;
}

/* a static constant data member with no object here (a class template's): its initializer, parsed again
   where it is used, as a value of its type (0: not built) */
VPRIV uint32_t cxx_gen_static_member_value(cxx *c, const cxx_entity *v, uint32_t loc)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure, edecl = c->edecl_name;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;
	static __thread int depth;

	bool record = v->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, v->ty) && AST_TYPE_KIND(c->ast, v->ty) != TY_ARRAY;

	if (!cxx_gen_on(c) || !v->nsdmi_pos || !v->nsdmi_scope || v->ty == CXX_TY_UNKNOWN || (!record && !cxx_gen_type_ok(c, v->ty)) ||
	    cxx_gen_is_reference(c, v->ty) || depth > 8 || (record && !cxx_gen_class_ok2(c, v->ty, true)))
		return 0;
	depth++;
	c->pos = v->nsdmi_pos + (c->tok[v->nsdmi_pos].kind == LEX_TOKEN_EQUAL), c->scope = v->nsdmi_scope, c->fail = &jb;
	if (!setjmp(jb)) {
		if (record && (cxx_is(c, LEX_TOKEN_LEFT_PAREN) || cxx_is(c, LEX_TOKEN_LEFT_BRACE))) {
			/* T X::m(args), T X::m{args}: a temporary made the same way (a value use: its copy is the object's) */
			uint32_t an[16], n;
			bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE);

			if (braced)
				cxx_braced_arguments_parse(c);
			else
				cxx_call_arguments_parse(c);
			n = c->ncargs;
			if (n <= 16) {
				memcpy(an, c->carg_nodes, n * sizeof *an);
				cxx_ctor_use(c, QUNQ(v->ty), c->cargs, c->cargs_lval, n, braced);
				cxx_gen_construct_temp(c, QUNQ(v->ty), an, n, loc);
				node = c->enode;
			}
		} else {
			cxx_assignment_expression_parse(c);
			if (c->enode && (cxx_is(c, LEX_TOKEN_SEMICOLON) || cxx_is(c, LEX_TOKEN_COMMA)))
				node = record ? (QUNQ(AST_NODE_AT(c->ast, c->enode)->ty) == QUNQ(v->ty) ? c->enode : 0) :
					cc_conversion_apply(c->ast, cc_conversion_decay(c->ast, c->enode), QUNQ(v->ty));
		}
	}
	depth--;
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee, c->edecl_name = edecl;
	return node;
}

/* op x with the built-in operator: + - ~ ! * & ++ -- (prefix) */
VPRIV void cxx_gen_unary(cxx *c, int op, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t r;

	if (!cxx_gen_on(c) || !x)
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && op == LEX_TOKEN_EXCLAMATION) {
		x = cxx_gen_convert_class(c, x, cc_type_basic(TY_BOOL), loc);	/* !x: x's operator bool */
		if (!x)
			return;
	}
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && op != LEX_TOKEN_AMPERSAND)
		return;	/* (a class operand of a built-in operator: a conversion function, not built yet) */
	switch (op) {
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS: case LEX_TOKEN_TILDE:
		x = cc_node_rvalue(a, x);
		if (!cc_type_is_arithmetic(a, AST_NODE_AT(a, x)->ty))
			return;
		x = cc_conversion_apply(a, x, cc_node_promoted_type(a, x));
		r = cc_node_new(a, N_UNARY, AST_NODE_AT(a, x)->ty, loc);
		AST_NODE_AT(a, r)->aux = (uint16_t)op;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_EXCLAMATION:
		x = cc_condition_scalar(a, cc_node_rvalue(a, x), "'!'");
		r = cc_node_new(a, N_UNARY, cc_type_basic(TY_INT), loc);
		AST_NODE_AT(a, r)->aux = LEX_TOKEN_EXCLAMATION;
		AST_NODE_AT(a, r)->a = x;
		break;
	case LEX_TOKEN_STAR:
		r = cc_dereference_build(a, x, loc);
		break;
	case LEX_TOKEN_AMPERSAND:
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		r = cc_address_of_build(a, x, loc);
		break;
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS:
		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? N_PREINC : N_PREDEC, QUNQ(AST_NODE_AT(a, x)->ty), loc);
		AST_NODE_AT(a, r)->a = x;
		if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_BOOL)
			AST_NODE_AT(a, r)->flags |= NF_LVALUE;	/* (C++: ++x is an lvalue) */
		break;
	default:
		return;
	}
	c->enode = r;
}

/* x++ or x-- */
VPRIV void cxx_gen_postfix(cxx *c, int op, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t r;

	if (!cxx_gen_on(c) || !x || !(AST_NODE_AT(a, x)->flags & NF_LVALUE))
		return;
	r = cc_node_new(a, op == LEX_TOKEN_PLUS_PLUS ? N_POSTINC : N_POSTDEC, QUNQ(AST_NODE_AT(a, x)->ty), loc);
	AST_NODE_AT(a, r)->a = x;
	c->enode = r;
}

/* a string literal: its bytes (no escapes), NUL-terminated */
VPRIV void cxx_gen_string(cxx *c, const char *text, size_t len, uint32_t loc)
{
	cc *a = c->ast;
	char *buf;
	uint32_t si, n;

	if (!cxx_gen_on(c))
		return;
	buf = cc_heap_allocate(a, len + 1);
	memcpy(buf, text, len);
	buf[len] = 0;
	si = a->nstr++;
	a->strs[si] = (cstr){ buf, (uint32_t)len + 1, 1, LEX_TOKEN_STRING };
	n = cc_node_new(a, N_STR, cc_type_array(a, QT(TY_CHAR, Q_CONST), (uint64_t)len + 1, 0), loc);
	AST_NODE_AT(a, n)->a = si;
	AST_NODE_AT(a, n)->flags = NF_LVALUE;
	c->enode = n;
}

/* (T)x, static_cast<T>(x), T(x) between scalar types */
VPRIV void cxx_gen_cast(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN)
		return;
	if (AST_TYPE_KIND(a, to) == TY_VOID) {	/* (void)x */
		c->enode = cc_cast_insert(a, cc_node_rvalue(a, x), cc_type_basic(TY_VOID), CK_VOID);
		return;
	}
	if (cxx_gen_is_reference(c, to)) {
		/* static_cast<T&&>(x), static_cast<T&>(x), const_cast<T&>(x) of an lvalue of that type: the same object
		   (a class: the same record, however its type is spelled; const dropped through its address) */
		qty xt = QUNQ(AST_NODE_AT(a, x)->ty), bt = QUNQ(AST_TYPE(a, to)->base);
		bool same = xt == bt || (cc_type_is_record(a, xt) && cc_type_is_record(a, bt) && AST_TYPE_KIND(a, xt) == AST_TYPE_KIND(a, bt) &&
					 AST_TYPE(a, xt)->aux == AST_TYPE(a, bt)->aux);

		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && same) {
			if (AST_NODE_AT(a, x)->ty != AST_TYPE(a, to)->base) {
				qty pt = cc_type_pointer(a, AST_TYPE(a, to)->base);
				uint32_t addr = cc_address_of_build(a, x, loc);

				x = cc_dereference_build(a, cc_cast_insert(a, addr, pt, CK_NOP), loc);
			}
			c->enode = x;
		}
		return;
	}
	if (!cxx_gen_type_ok(c, to) || cc_type_is_record(a, to))
		return;
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty) || cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, AST_NODE_AT(a, x)->ty, QUNQ(to), x));
}

/* an object of an empty class with no node (a constexpr reference to a function object, json's to_json):
   a temporary of it does as well, nothing in it being read (0: not an empty class) */
VPRIV uint32_t cxx_gen_empty_object(cxx *c, qty t, uint32_t loc)
{
	bool pod, empty;
	uint32_t tmp;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == TY_ARRAY ||
	    !cxx_class_layout_info(c, QUNQ(t), &pod, &empty) || !empty || !pod)
		return 0;
	tmp = cxx_gen_temporary(c, QUNQ(t), loc);
	return tmp ? cxx_gen_var_node(c, tmp, loc) : 0;
}

/* nullptr: a null void pointer (it converts to any pointer type) */
VPRIV void cxx_gen_nullptr(cxx *c, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	if (!cxx_gen_on(c))
		return;
	c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), loc), vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, 0));
}

/* reinterpret_cast<T>(x) between pointers, or a pointer and an integer of its size */
VPRIV void cxx_gen_reinterpret(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;
	qty from;
	bool pf, pt;

	(void)loc;
	if (!cxx_gen_on(c) || !x || to == CXX_TY_UNKNOWN || cxx_gen_is_reference(c, to) || !cxx_gen_type_ok(c, to))
		return;
	x = cc_conversion_decay(a, x);
	from = AST_NODE_AT(a, x)->ty;
	pf = AST_TYPE_KIND(a, from) == TY_PTR, pt = AST_TYPE_KIND(a, to) == TY_PTR;
	if (!cxx_gen_type_ok(c, from) || !(pf || pt) || (!pf && !cc_type_is_integer(a, from)) || (!pt && !cc_type_is_integer(a, to)))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, from, QUNQ(to), x));
}

/* a[i] on a pointer or an array: *(a + i) */
VPRIV void cxx_gen_index(cxx *c, uint32_t base, uint32_t index, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !base || !index)
		return;
	c->enode = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, base, index, loc), loc);
}

/* cond ? x : y of scalars */
VPRIV void cxx_gen_conditional(cxx *c, uint32_t cond, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !cond || !x || !y || cc_type_is_record(a, AST_NODE_AT(a, cond)->ty))
		return;
	if (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) != cc_type_is_record(a, AST_NODE_AT(a, y)->ty) ||
	    (cc_type_is_record(a, AST_NODE_AT(a, x)->ty) && (QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(AST_NODE_AT(a, y)->ty) ||
							      cxx_gen_trivial_abi(c, AST_NODE_AT(a, x)->ty, 0) != 1)))
		return;	/* (a class's: both of one class that copies as bytes, as C's ?: of structs) */
	cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
	c->enode = cc_conditional_build(a, cond, x, y, loc);
}

/* a <=> b of scalars: a std::strong_ordering or partial_ordering object (libc++'s: one signed char,
   -1 0 1, -127 unordered) in a temporary */
VPRIV void cxx_gen_spaceship(cxx *c, uint32_t l, uint32_t r, qty t, uint32_t loc)
{
	cc *a = c->ast;
	const tag *rec;
	uint32_t tmp, v, lt, gt, node;
	bool partial;

	c->enode = 0;
	if (!cxx_gen_on(c) || !l || !r || t == CXX_TY_UNKNOWN || !cc_type_is_record(a, t))
		return;
	rec = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	if (rec->nfield < 1 || !rec->f[0].name || AST_TYPE(a, rec->f[0].ty)->size != 1 || AST_TYPE(a, QUNQ(t))->size != 1)
		return;	/* (not libc++'s layout) */
	partial = !cc_type_is_integer(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, cc_node_rvalue(a, l))->ty) != TY_PTR;
	if (!(tmp = cxx_gen_temporary(c, QUNQ(t), loc)))
		return;
	/* (each operand read twice: the operands are values here, as a built-in comparison's are) */
	lt = cc_binary_build(a, LEX_TOKEN_LESS, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	gt = cc_binary_build(a, LEX_TOKEN_GREATER, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc);
	v = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
	if (partial)
		v = cc_conditional_build(a, cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, l), cc_node_rvalue(a, r), loc), v,
					 cc_node_integer_new(a, -127, cc_type_basic(TY_INT), loc), loc);
	v = cc_conditional_build(a, gt, cc_node_integer_new(a, 1, cc_type_basic(TY_INT), loc), v, loc);
	v = cc_conditional_build(a, lt, cc_node_integer_new(a, -1, cc_type_basic(TY_INT), loc), v, loc);
	v = cc_assignment_build(a, 0, member(a, cxx_gen_var_node(c, tmp, loc), rec->f[0].name, loc),
				cc_conversion_apply(a, v, QUNQ(rec->f[0].ty)), loc);
	(void)node;
	c->enode = cc_node_new_operands(a, N_COMMA, QUNQ(t), loc, v, cxx_gen_var_node(c, tmp, loc));	/* (the ordering object, a value) */
}

/* x, y */
VPRIV void cxx_gen_comma(cxx *c, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c) || !x || !y)
		return;
	y = cc_conversion_decay(a, y);
	c->enode = cc_node_new_operands(a, N_COMMA, QUNQ(AST_NODE_AT(a, y)->ty), loc, x, y);
}

/* l op r with the built-in operator (the operands' nodes; 0: one was not built) */
/*
 * a @ b rewritten as (a <=> b) @ 0 (C++20): `ord` the ordering object operator<=> gave (std::strong_ordering,
 * weak_ordering, partial_ordering: libc++'s __value_ is -1, 0, 1, or -127 for unordered), tested so that an
 * unordered value is false for every @.  c->enode: the bool (0: not built).
 */
VPRIV void cxx_gen_ordering_test(cxx *c, uint32_t ord, int op, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t v, t1, t2;
	qty it = cc_type_basic(TY_INT);

	c->enode = 0;
	if (!cxx_gen_on(c) || !ord || !(v = cxx_gen_member_of(c, ord, vintern_cstr("__value_"), loc, 0)))
		return;
#define CMPV(k) cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_conversion_apply(a, cc_node_rvalue(a, v), it), cc_node_integer_new(a, (int64_t)(k), it, loc), loc)
	if (op == LEX_TOKEN_LESS)
		c->enode = CMPV(-1);
	else if (op == LEX_TOKEN_GREATER)
		c->enode = CMPV(1);
	else {
		t1 = CMPV(0);
		t2 = op == LEX_TOKEN_LESS_EQUAL ? CMPV(-1) : CMPV(1);
		c->enode = cc_binary_build(a, LEX_TOKEN_PIPE_PIPE, t1, t2, loc);
	}
#undef CMPV
}

VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (!cxx_gen_on(c) || !l || !r)
		return;
	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND || op == LEX_TOKEN_PIPE_PIPE) {
		/* a class operand of && and ||: its operator bool */
		if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
			l = cxx_gen_convert_class(c, l, cc_type_basic(TY_BOOL), loc);
		if (r && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty))
			r = cxx_gen_convert_class(c, r, cc_type_basic(TY_BOOL), loc);
		if (!l || !r)
			return;
	}
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) || cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty))
		return;	/* (a class operand of a built-in operator: a conversion function, not built yet) */
	c->enode = cc_binary_build(c->ast, op, l, r, loc);
}

/* l = r, l op= r (op: the assignment operator's token) */
VPRIV void cxx_gen_assignment(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	int base;

	if (!cxx_gen_on(c) || !l || !r)
		return;
	switch (op) {
	case LEX_TOKEN_EQUAL: base = 0; break;
	case LEX_TOKEN_PLUS_EQUAL: base = LEX_TOKEN_PLUS; break;
	case LEX_TOKEN_MINUS_EQUAL: base = LEX_TOKEN_MINUS; break;
	case LEX_TOKEN_STAR_EQUAL: base = LEX_TOKEN_STAR; break;
	case LEX_TOKEN_SLASH_EQUAL: base = LEX_TOKEN_SLASH; break;
	case LEX_TOKEN_PERCENT_EQUAL: base = LEX_TOKEN_PERCENT; break;
	case LEX_TOKEN_AMPERSAND_EQUAL: base = LEX_TOKEN_AMPERSAND; break;
	case LEX_TOKEN_PIPE_EQUAL: base = LEX_TOKEN_PIPE; break;
	case LEX_TOKEN_CARET_EQUAL: base = LEX_TOKEN_CARET; break;
	case LEX_TOKEN_LESS_LESS_EQUAL: base = LEX_TOKEN_LESS_LESS; break;
	case LEX_TOKEN_GREATER_GREATER_EQUAL: base = LEX_TOKEN_GREATER_GREATER; break;
	default: return;
	}
	if (!(AST_NODE_AT(c->ast, l)->flags & NF_LVALUE))
		return;
	if (!base && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) != TY_ARRAY &&
	    QUNQ(AST_NODE_AT(c->ast, l)->ty) != QUNQ(AST_NODE_AT(c->ast, r)->ty) && cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) == 1) {
		/* x = v of a trivially copyable class from another type: the converting constructor's temporary,
		   copied (what the implicit operator= does) */
		r = cxx_gen_bind(c, r, QUNQ(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r || QUNQ(AST_NODE_AT(c->ast, r)->ty) != QUNQ(AST_NODE_AT(c->ast, l)->ty))
			return;
	}
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty) &&
	    (base || QUNQ(AST_NODE_AT(c->ast, l)->ty) != QUNQ(AST_NODE_AT(c->ast, r)->ty) || cxx_gen_trivial_abi(c, AST_NODE_AT(c->ast, l)->ty, 0) != 1))
		return;	/* (a class whose assignment is not a copy of its bytes: the implicit operator=, not built yet) */
	if (cc_type_is_record(c->ast, AST_NODE_AT(c->ast, r)->ty) != cc_type_is_record(c->ast, AST_NODE_AT(c->ast, l)->ty))
		return;
	if (!base && AST_TYPE_KIND(c->ast, AST_NODE_AT(c->ast, l)->ty) == TY_PTR && !cxx_gen_is_reference(c, AST_NODE_AT(c->ast, l)->ty)) {
		/* a pointer: the value converted as C++ does (Derived * to Base *, moved to the base subobject) */
		r = cxx_gen_bind(c, r, QUNQ(AST_NODE_AT(c->ast, l)->ty), loc);
		if (!r)
			return;
	}
	c->enode = cc_assignment_build(c->ast, base, l, r, loc);
}

/* the k-th default argument of f, parsed where the call is (in the scope of f's declaration): its node (0: not built) */
static uint32_t cxx_gen_default_argument(cxx *c, const cxx_entity *f, uint32_t k)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	volatile uint32_t node = 0;

	if (!f->defs || k >= f->ndefaults || !f->def_scope)
		return 0;
	c->pos = f->defs[2 * k], c->scope = f->def_scope, c->fail = &jb;
	if (!setjmp(jb)) {
		cxx_assignment_expression_parse(c);
		if (c->pos == f->defs[2 * k + 1])
			node = c->enode;
	}
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee;
	return node;
}

/* a function named as a value (not called): its designator */
VPRIV void cxx_gen_function_value(cxx *c, cxx_entity *f, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!cxx_gen_name(c, f) || !f->sym)
		return;
	n = cc_node_new(a, N_VAR, a->syms[f->sym].ty, loc);
	AST_NODE_AT(a, n)->a = f->sym;
	AST_NODE_AT(a, n)->flags = NF_LVALUE;
	c->enode = n;
}

/* f(args): the function overload resolution picked, the arguments converted to its parameters' types */
/* VCC_CXX_GEN=3: where cxx_gen_call gave up on a call, and the function called */
#define CXX_CALL_WHY() do { \
		static __thread int why_ = -1; \
		if (why_ < 0) \
			why_ = getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 3; \
		if (why_ && f && cxx_gen_on(c)) \
			fprintf(stderr, "no call (codegen.c:%d): %.*s/%u%s%.*s\n", __LINE__, (int)vintern_ents[f->name].len, vintern_ents[f->name].s, nargs, \
				f->member_of && f->member_of->owner ? " of " : "", f->member_of && f->member_of->owner ? (int)vintern_ents[f->member_of->owner->name].len : 0, \
				f->member_of && f->member_of->owner ? vintern_ents[f->member_of->owner->name].s : ""); \
	} while (0)

/* operator==(X, X) = default of a class whose members are scalars: the members compared in order, inline (0: not built) */
static uint32_t cxx_gen_defaulted_eq(cxx *c, uint32_t x, uint32_t y, uint32_t loc)
{
	cc *a = c->ast;
	qty t = QUNQ(AST_NODE_AT(a, x)->ty);
	const tag *r;
	uint32_t e = 0;

	if (!cc_type_is_record(a, t) || QUNQ(AST_NODE_AT(a, y)->ty) != t || AST_TYPE_KIND(a, t) != TY_STRUCT ||
	    !(AST_NODE_AT(a, x)->flags & NF_LVALUE) || !(AST_NODE_AT(a, y)->flags & NF_LVALUE))
		return 0;
	r = &a->tags[AST_TYPE(a, t)->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		int k = AST_TYPE_KIND(a, r->f[i].ty);
		uint32_t q;

		if (!r->f[i].name || !((k >= TY_BOOL && k <= TY_LDOUBLE) || k == TY_PTR || k == TY_ENUM))
			return 0;
		q = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, member(a, x, r->f[i].name, loc)),
				    cc_node_rvalue(a, member(a, y, r->f[i].name, loc)), loc);
		e = e ? cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, e, q, loc) : q;
	}
	return e ? cc_conversion_apply(a, e, cc_type_basic(TY_BOOL)) : cc_node_integer_new(a, 1, cc_type_basic(TY_BOOL), loc);
}

/* is f std::forward, std::move, std::move_if_noexcept or std::as_const (in std or libc++'s std::__1)? */
static bool cxx_gen_std_cast_function(cxx *c, const cxx_entity *f)
{
	const vident *v = &vintern_ents[f->name];
	const cxx_scope *s = f->home;
	bool std_ns = false;

	if (!((v->len == 7 && !memcmp(v->s, "forward", 7)) || (v->len == 4 && !memcmp(v->s, "move", 4)) ||
	      (v->len == 16 && !memcmp(v->s, "move_if_noexcept", 16)) || (v->len == 8 && !memcmp(v->s, "as_const", 8))))
		return false;
	if (!s || f->member_of || s->kind != CXX_SCOPE_NAMESPACE || !s->owner)
		return false;
	if (s->parent == c->global)
		std_ns = vintern_ents[s->owner->name].len == 3 && !memcmp(vintern_ents[s->owner->name].s, "std", 3);
	else if (s->parent && s->parent->kind == CXX_SCOPE_NAMESPACE && s->parent->owner && s->parent->parent == c->global)
		std_ns = vintern_ents[s->parent->owner->name].len == 3 && !memcmp(vintern_ents[s->parent->owner->name].s, "std", 3);
	return std_ns;
}

/* f({...}): a temporary of class type t constructed from the braced list at token `pos` (its arguments
   parsed again here for their code: the list's first parse was unevaluated), as an lvalue (0: not built) */
static uint32_t cxx_gen_braced_temp(cxx *c, qty t, uint32_t pos, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t save = c->pos, an[16], n, tmp;
	qty at[16];
	uint8_t al[16];
	cc_id_list l;

	t = QUNQ(t);
	if (!cc_type_is_record(a, t) || AST_TYPE_KIND(a, t) == TY_ARRAY || !cxx_gen_object_type_ok(c, t))
		return 0;
	c->pos = pos;
	cxx_braced_arguments_parse(c);
	c->pos = save;
	n = c->ncargs;
	if (n == UINT32_MAX || n > 16)
		return 0;
	memcpy(an, c->carg_nodes, sizeof an), memcpy(at, c->cargs, n * sizeof *at), memcpy(al, c->cargs_lval, n);
	if (cxx_is_initializer_list(c, t)) {
		/* f(initializer_list<E>) from {a, b}: the list itself (its array's class elements, whose destruction
		   at the full-expression's end is not built: not done) */
		uint32_t arr = 0, il;

		c->ctor_ilist_n = n;
		memcpy(c->ctor_ilist_types, at, n * sizeof *at), memcpy(c->ctor_ilist_lval, al, n);
		memcpy(c->ctor_ilist_brace, c->carg_brace, sizeof c->ctor_ilist_brace);
		if (!c->gen_block)
			return 0;
		il = cxx_gen_initializer_list(c, t, an, n, c->gen_block, loc, &arr);	/* (before the call, in its block) */
		if (!il || (arr && cc_type_is_record(a, AST_TYPE(a, a->syms[arr].ty)->base) &&
			    cxx_gen_needs_destruction(c, AST_TYPE(a, a->syms[arr].ty)->base, 0)))
			return 0;
		return il;
	}
	cxx_ctor_use(c, t, at, al, n, true);
	{
		cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
		bool found = c->ctor_found, none = c->ctor_none;

		if (!(tmp = cxx_gen_class_temp(c, t, loc)))
			return 0;
		c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none, c->ctor_ilist = il;
	}
	(void)l;
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, tmp, loc), t, an, n, c->gen_block, loc))
		return 0;	/* (constructed before the call, in its block: the temporary is an lvalue) */
	return cxx_gen_var_node(c, tmp, loc);
}

/* is a defaulted copy or move constructor of class t trivial: no vptr, its bases' and members' copies
   trivial (the class's other copy constructors aside: iter_impl's user-provided one does not make its
   defaulted move constructor non-trivial) */
static bool cxx_gen_defaulted_copy_trivial(cxx *c, qty t)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (!cxx_class_layout_info(c, t, &pod, &empty))
		return false;
	if (pod)
		return true;
	if (!cls || cxx_class_polymorphic(c, t))
		return false;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_trivial_abi(c, bases[i], 1) != 1)
			return false;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN || (cc_type_is_record(a, ft) && cxx_gen_trivial_abi(c, ft, 1) != 1))
			return false;
	}
	return true;
}

VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list list;
	uint32_t fn, n, self = 0, pre = 0, ret_tmp = 0, self_at = 0, call, all[32], br[16];

	memcpy(br, c->gen_call_brace, sizeof br);	/* (this call's alone: taken at once) */
	memset(c->gen_call_brace, 0, sizeof c->gen_call_brace);
	c->enode = 0;	/* (a call that is not built has no node: not its last argument's, which the parse left here) */

	if (f && f->fn_defaulted && cxx_gen_on(c) && f->name == vintern_cstr("operator==")) {
		/* a defaulted equality: no function to call, the comparison itself (a friend's two operands; a
		   member's object and one) */
		uint32_t x = f->member_of && !f->fn_static ? (object ? (arrow ? cc_dereference_build(a, cc_conversion_decay(a, object), loc) : object) : 0) :
			     nargs == 2 ? args[0] : 0;
		uint32_t y = f->member_of && !f->fn_static ? (nargs == 1 ? args[0] : 0) : nargs == 2 ? args[1] : 0;

		c->enode = x && y ? cxx_gen_defaulted_eq(c, x, y, loc) : 0;
		if (!c->enode)
			CXX_CALL_WHY();
		return;
	}

	if (!cxx_gen_on(c) || !f || f->kind != CXX_ENTITY_VALUE || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC || nargs > 16)
		{ CXX_CALL_WHY(); return; }
	if (f->fn_defaulted && nargs == 1 && object && !arrow && args[0] && cxx_gen_is_special(f) && vintern_ents[f->name].s[1] == 'c' &&
	    f->member_of && f->member_of->owner && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(f->member_of->owner->ty) &&
	    QUNQ(AST_NODE_AT(a, object)->ty) == QUNQ(f->member_of->owner->ty) && cxx_gen_defaulted_copy_trivial(c, f->member_of->owner->ty)) {
		/* T(const T&) = default or T(T&&) = default of a trivially copyable class: its bytes (clang calls no
		   function for it, and defines none) */
		qty t = QUNQ(f->member_of->owner->ty);
		bool pod, empty;

		if (cxx_class_layout_info(c, t, &pod, &empty) && empty)
			c->enode = cc_node_rvalue(a, args[0]);	/* (an empty class: nothing; its byte may be another's) */
		else {
			uint32_t target = object;

			if (AST_NODE_AT(a, target)->ty != t) {
				qty pt = cc_type_pointer(a, t);
				uint32_t addr = cc_address_of_build(a, target, loc);

				target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
			}
			c->enode = cc_assignment_build(a, 0, target, cc_node_rvalue(a, args[0]), loc);
		}
		return;
	}
	ft = AST_TYPE(a, f->ty);
	uint32_t bargs[16];
	{
		/* f({...}): a braced list argument (no node) made a temporary of its parameter's class */
		bool any = false;

		for (uint32_t i = 0; i < nargs; i++)
			any |= !args[i] && br[i] && i < ft->n;
		if (any) {
			/* (what building them leaves of the statement's state is not the statement's: a call that fails
			   after them must still fail its statement) */
			uint32_t sdone = c->sdone, snode = c->snode;

			memcpy(bargs, args, nargs * sizeof *args);
			for (uint32_t i = 0; i < nargs; i++)
				if (!args[i] && br[i] && i < ft->n) {
					qty pt = a->params[ft->aux + i];

					if (pt != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, pt) == TY_PTR && AST_TYPE(a, pt)->ext)
						pt = AST_TYPE(a, pt)->base;
					bargs[i] = cxx_gen_braced_temp(c, pt, br[i], loc);
				}
			c->sdone = sdone, c->snode = snode;
			c->enode = 0;	/* (what building them left is no node of this call's: a call that fails has none) */
			args = bargs;
		}
	}
	if (nargs < ft->n && ft->n <= 32 && ft->n - nargs <= f->ndefaults && f->defs && !(ft->flags & TF_VARIADIC)) {
		/* the arguments left out: the parameters' default arguments */
		uint32_t n = ft->n, first = n - f->ndefaults;

		memcpy(all, args, nargs * sizeof *args);
		for (uint32_t i = nargs; i < n; i++)
			if (!(all[i] = cxx_gen_default_argument(c, f, i - first)))
				{ CXX_CALL_WHY(); return; }
		args = all, nargs = n;
		ft = AST_TYPE(a, f->ty);
	}
	if ((ft->flags & TF_VARIADIC) ? nargs < ft->n || nargs > 32 : ft->n != nargs)
		{ CXX_CALL_WHY(); return; }	/* (default arguments not recorded: not built yet) */
	if (nargs == 1 && f->is_instance && cxx_gen_is_reference(c, ft->base) && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE) &&
	    cxx_gen_std_cast_function(c, f)) {
		/* std::forward, std::move, std::move_if_noexcept, std::as_const of an lvalue: that object (clang's
		   builtins: no function called, none emitted) */
		c->enode = args[0];
		return;
	}
	if (!cxx_gen_value_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID)
		{ CXX_CALL_WHY(); return; }
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i] || (i < ft->n ? !cxx_gen_value_ok(c, a->params[ft->aux + i]) :
				 cc_type_is_record(a, AST_NODE_AT(a, args[i])->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, args[i])->ty) != TY_ARRAY))
			{ CXX_CALL_WHY(); return; }	/* (a class through ...: not built yet) */
	if (!cxx_gen_on(c) || !cxx_gen_name(c, f) || !f->sym)
		{ CXX_CALL_WHY(); return; }
	if (f->name_guess && !(a->syms[f->sym].flags & SF_DEFINED))
		{ CXX_CALL_WHY(); return; }	/* (a name only vcc gives, and no code of vcc's for it: nothing would define it) */
	ft = AST_TYPE(a, f->ty);
	cc_id_list_init(&list);
	if (cxx_gen_by_address(c, ft->base)) {
		/* a class returned by address: in a temporary (whoever takes the value may aim it elsewhere) */
		ret_tmp = cxx_gen_class_temp(c, QUNQ(ft->base), loc);
		if (!ret_tmp)
			{ CXX_CALL_WHY(); return; }
		cc_id_list_append(a, &list, cc_address_of_build(a, cxx_gen_var_node(c, ret_tmp, loc), loc));
		self_at = 1;
	}
	if (cxx_gen_has_this(f)) {
		/* the object: its address (obj.f(), p->f()), or this for a member named alone */
		qty tt = cxx_gen_this_type(c, f);

		if (object)
			self = arrow ? cc_conversion_decay(a, object) : (AST_NODE_AT(a, object)->flags & NF_LVALUE) ? cc_address_of_build(a, object, loc) : 0;
		else if (c->gen_this) {
			cxx_gen_this(c, loc);
			self = c->enode;
			c->enode = 0;
		}
		if (!self || tt == CXX_TY_UNKNOWN)
			{ CXX_CALL_WHY(); goto fail; }	/* (a member called on a value: a temporary, not built yet) */
		self = cxx_gen_base_pointer(c, self, AST_TYPE(a, tt)->base, loc);	/* (a base class's member: on the base subobject) */
		if (!self)
			{ CXX_CALL_WHY(); goto fail; }
		cc_id_list_append(a, &list, self);
	}
	for (uint32_t i = 0; i < nargs; i++) {
		qty pt = i < ft->n ? a->params[ft->aux + i] : CXX_TY_UNKNOWN;
		uint32_t arg;

		if (i >= ft->n) {
			/* through ...: the default argument promotions (an array decays, a float is a double) */
			arg = cc_node_rvalue(a, args[i]);
			if (arg && AST_TYPE_KIND(a, AST_NODE_AT(a, arg)->ty) == TY_FLOAT)
				arg = cc_cast_insert(a, arg, cc_type_basic(TY_DOUBLE), CK_F2F);
			if (arg && !cxx_gen_type_ok(c, AST_NODE_AT(a, arg)->ty))
				arg = 0;
		} else if (cxx_gen_by_address(c, pt))
			arg = cxx_gen_class_argument(c, args[i], pt, loc);
		else if (args[i] && cc_type_is_record(a, pt) && AST_TYPE_KIND(a, pt) != TY_ARRAY && QUNQ(AST_NODE_AT(a, args[i])->ty) != QUNQ(pt) &&
			 !(c->nullptr_ty && QUNQ(pt) == c->nullptr_ty)) {	/* (nullptr_t: a struct here, bound as before) */
			/* a class passed as a value (registers) from another type: a temporary made by its converting
			   constructor, its value passed (const_iterator from iterator) */
			uint32_t p = cxx_gen_class_argument(c, args[i], pt, loc);

			arg = p ? cc_node_rvalue(a, cc_dereference_build(a, p, loc)) : 0;
		} else
			arg = cxx_gen_bind(c, args[i], pt, loc);

		if (!arg) {
			CXX_CALL_WHY(); goto fail;
		}
		cc_id_list_append(a, &list, arg);
	}
	if (f->fn_virtual && !cxx_gen_is_special(f) && c->gen_vmode != 1) {
		/* through the vtable: tmp = object's address; (*(F ***)tmp)[slot](tmp, args) */
		qty pf = cc_type_pointer(a, a->syms[f->sym].ty), ppf = cc_type_pointer(a, pf), pppf = cc_type_pointer(a, ppf);
		qty st = QUNQ(AST_NODE_AT(a, self)->ty);
		uint32_t tmp, tn, vptr;

		if (c->gen_vmode == 2 || !f->vslot || !self)
			{ CXX_CALL_WHY(); goto fail; }
		tmp = cxx_gen_temporary(c, st, loc);
		if (!tmp)
			{ CXX_CALL_WHY(); goto fail; }
		pre = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), self, loc);
		list.v[self_at] = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		tn = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		vptr = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, tn, pppf, cc_conversion_cast_kind(a, st, pppf, tn)), loc));
		fn = cc_node_rvalue(a, cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, vptr,
				cc_node_integer_new(a, f->vslot - 1, cc_type_basic(TY_LONG), loc), loc), loc));
	} else {
		/* (a constructor or destructor: the complete object's; a base subobject's: the base object's, C2/D2) */
		bool base_obj = c->gen_base_type && cxx_gen_is_special(f) && f->member_of && f->member_of->owner &&
				QUNQ(f->member_of->owner->ty) == c->gen_base_type;
		uint32_t target = base_obj ? f->sym : !f->sym1 ? f->sym : cxx_gen_is_special(f) && vintern_ents[f->name].s[1] == 'd' ? cxx_gen_dtor_sym(f) : f->sym1;

		if (base_obj)
			c->gen_base_type = 0;

		fn = cc_node_new(a, N_VAR, a->syms[target].ty, loc);
		AST_NODE_AT(a, fn)->a = target;
		AST_NODE_AT(a, fn)->flags = NF_LVALUE;
		fn = cc_node_rvalue(a, fn);	/* (the function designator: a pointer to it) */
	}
	n = cc_node_new(a, N_CALL, ret_tmp ? cc_type_pointer(a, QUNQ(ft->base)) : QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = list.n;
	call = n;
	if (pre)
		n = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, n)->ty, loc, pre, n);
	if (ret_tmp) {
		/* the call gives the result object's address back */
		uint32_t expr = n;

		n = cc_dereference_build(a, n, loc);
		cxx_gen_prv_add(c, n, expr, call, ret_tmp);
	} else if (cc_type_is_record(a, ft->base) && c->gen_block) {
		/* a class returned as C does: held in a temporary, so that it is an object (x().f(), a const T&) */
		uint32_t tmp = cxx_gen_temporary(c, QUNQ(ft->base), loc);

		if (!tmp)
			{ CXX_CALL_WHY(); goto fail; }
		n = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), n, loc);
		n = cc_node_new_operands(a, N_COMMA, cc_type_pointer(a, QUNQ(ft->base)), loc, n, cc_address_of_build(a, cxx_gen_var_node(c, tmp, loc), loc));
		n = cc_dereference_build(a, n, loc);
	}
	if (cxx_gen_is_reference(c, ft->base))
		n = cxx_gen_referent(c, n, loc);
	c->enode = n;
	return;
fail:
	if (ret_tmp)
		cxx_gen_fail(c);	/* (its temporary is declared) */
}

/* p(args), r(args), o.f(args): a call through a pointer or reference to a function (no class through ... or by
   address, no class result returned by address yet) */
VPRIV void cxx_gen_indirect_call(cxx *c, uint32_t callee, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	qty t;
	const ctype *ft;
	cc_id_list list;
	uint32_t n, fn;

	c->enode = 0;
	if (!cxx_gen_on(c) || !callee || nargs > 32)
		return;
	t = QUNQ(AST_NODE_AT(a, callee)->ty);
	if (AST_TYPE_KIND(a, t) == TY_PTR)
		t = QUNQ(AST_TYPE(a, t)->base);
	if (AST_TYPE_KIND(a, t) != TY_FUNC)
		return;
	ft = AST_TYPE(a, t);
	if ((ft->flags & TF_VARIADIC) ? nargs < ft->n : ft->n != nargs)
		return;
	if (AST_TYPE_KIND(a, ft->base) != TY_VOID && (!cxx_gen_value_ok(c, ft->base) || cxx_gen_by_address(c, ft->base) || cc_type_is_record(a, ft->base)))
		return;
	cc_id_list_init(&list);
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t arg;

		if (!args[i])
			return;
		if (i >= ft->n) {
			if (cc_type_is_record(a, AST_NODE_AT(a, args[i])->ty) && AST_TYPE_KIND(a, AST_NODE_AT(a, args[i])->ty) != TY_ARRAY)
				return;
			arg = cc_node_rvalue(a, args[i]);
			if (arg && AST_TYPE_KIND(a, AST_NODE_AT(a, arg)->ty) == TY_FLOAT)
				arg = cc_cast_insert(a, arg, cc_type_basic(TY_DOUBLE), CK_F2F);
		} else {
			qty pt = a->params[ft->aux + i];

			if (!cxx_gen_value_ok(c, pt))
				return;
			arg = cxx_gen_by_address(c, pt) ? cxx_gen_class_argument(c, args[i], pt, loc) : cxx_gen_bind(c, args[i], pt, loc);
		}
		if (!arg)
			return;
		cc_id_list_append(a, &list, arg);
	}
	fn = cc_node_rvalue(a, callee);	/* (a function designator decays to a pointer to it) */
	if (!fn)
		return;
	n = cc_node_new(a, N_CALL, QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = list.n;
	if (cxx_gen_is_reference(c, ft->base))
		n = cxx_gen_referent(c, n, loc);
	c->enode = n;
}

/*
 * An overloaded operator's call, after cxx_operator_call_type picked the function: x.operator@(args) for a
 * member, operator@(x, args) for a function at namespace scope (a friend too).
 */
VPRIV void cxx_gen_operator_call(cxx *c, uint32_t obj, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cxx_entity *f = c->call_picked;
	uint32_t all[17];

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!c->call_resolved || !f || f->kind != CXX_ENTITY_VALUE || !obj || nargs > 16)
		return;
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i])
			return;
	if (!cxx_gen_has_this(f) && !f->fn_static && f->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f->ty) == TY_FUNC &&
	    AST_TYPE(c->ast, f->ty)->n == nargs && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, obj)->ty)) {
		/* (a member of the object's class whose entity lacks its class, an instance's late member: found there) */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(c->ast, obj)->ty));

		for (cxx_entity *e = cls ? cxx_lookup_local(cls, f->name) : NULL; e; e = e->next)
			if (e == f) {
				f->member_of = cls;
				break;
			} else if (e->kind == CXX_ENTITY_VALUE && e->member_of == cls && e->ty == f->ty) {
				f = e;	/* (the same member, as the object's class declares it) */
				break;
			}
	}
	if (cxx_gen_has_this(f)) {
		cxx_gen_call(c, f, obj, false, args, nargs, loc);
		return;
	}
	all[0] = obj;
	memcpy(all + 1, args, nargs * sizeof *args);
	cxx_gen_call(c, f, 0, false, all, nargs + 1, loc);
}

static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

/* ---- builtins -------------------------------------------------------------------------------- */

/* the builtins that are a C library function (or operator new/delete) by another name */
static const struct cxx_builtin_function {
	const char *name, *symbol;
	char ret, params[4];	/* v void, p void *, q const void *, s const char *, z size_t, i int, f float, d double, c char */
} cxx_builtin_functions[] = {
	{ "__builtin_memcpy", "memcpy", 'p', "pqz" }, { "__builtin_memmove", "memmove", 'p', "pqz" },
	{ "__builtin_memset", "memset", 'p', "piz" }, { "__builtin_memcmp", "memcmp", 'i', "qqz" },
	{ "__builtin_strlen", "strlen", 'z', "s" }, { "__builtin_memchr", "memchr", 'p', "qiz" },
	{ "__builtin_strcmp", "strcmp", 'i', "ss" }, { "__builtin_unreachable", "abort", 'v', "" },
	{ "__builtin_trap", "abort", 'v', "" }, { "__builtin_abort", "abort", 'v', "" },
	{ "__builtin_operator_new", "_Znwm", 'p', "z" }, { "__builtin_operator_delete", "_ZdlPv", 'v', "p" },
	{ "__builtin_operator_new", "_ZnwmSt11align_val_t", 'p', "zz" }, { "__builtin_operator_delete", "_ZdlPvm", 'v', "pz" },
	{ "__builtin_operator_delete", "_ZdlPvmSt11align_val_t", 'v', "pzz" },
	/* (new and delete expressions: cxx_gen_new, cxx_gen_delete) */
	{ "__vcc_new_array", "_Znam", 'p', "z" }, { "__vcc_delete_array", "_ZdaPv", 'v', "p" },
	{ "__builtin_sin", "sin", 'd', "d" }, { "__builtin_sinf", "sinf", 'f', "f" },
	{ "__builtin_cos", "cos", 'd', "d" }, { "__builtin_cosf", "cosf", 'f', "f" },
	{ "__builtin_tan", "tan", 'd', "d" }, { "__builtin_tanf", "tanf", 'f', "f" },
	{ "__builtin_asin", "asin", 'd', "d" }, { "__builtin_asinf", "asinf", 'f', "f" },
	{ "__builtin_acos", "acos", 'd', "d" }, { "__builtin_acosf", "acosf", 'f', "f" },
	{ "__builtin_atan", "atan", 'd', "d" }, { "__builtin_atanf", "atanf", 'f', "f" },
	{ "__builtin_sinh", "sinh", 'd', "d" }, { "__builtin_sinhf", "sinhf", 'f', "f" },
	{ "__builtin_cosh", "cosh", 'd', "d" }, { "__builtin_coshf", "coshf", 'f', "f" },
	{ "__builtin_tanh", "tanh", 'd', "d" }, { "__builtin_tanhf", "tanhf", 'f', "f" },
	{ "__builtin_exp", "exp", 'd', "d" }, { "__builtin_expf", "expf", 'f', "f" },
	{ "__builtin_log", "log", 'd', "d" }, { "__builtin_logf", "logf", 'f', "f" },
	{ "__builtin_log10", "log10", 'd', "d" }, { "__builtin_log10f", "log10f", 'f', "f" },
	{ "__builtin_log2", "log2", 'd', "d" }, { "__builtin_log2f", "log2f", 'f', "f" },
	{ "__builtin_sqrt", "sqrt", 'd', "d" }, { "__builtin_sqrtf", "sqrtf", 'f', "f" },
	{ "__builtin_cbrt", "cbrt", 'd', "d" }, { "__builtin_cbrtf", "cbrtf", 'f', "f" },
	{ "__builtin_fabs", "fabs", 'd', "d" }, { "__builtin_fabsf", "fabsf", 'f', "f" },
	{ "__builtin_floor", "floor", 'd', "d" }, { "__builtin_floorf", "floorf", 'f', "f" },
	{ "__builtin_ceil", "ceil", 'd', "d" }, { "__builtin_ceilf", "ceilf", 'f', "f" },
	{ "__builtin_round", "round", 'd', "d" }, { "__builtin_roundf", "roundf", 'f', "f" },
	{ "__builtin_trunc", "trunc", 'd', "d" }, { "__builtin_truncf", "truncf", 'f', "f" },
	{ "__builtin_exp2", "exp2", 'd', "d" }, { "__builtin_exp2f", "exp2f", 'f', "f" },
	{ "__builtin_expm1", "expm1", 'd', "d" }, { "__builtin_expm1f", "expm1f", 'f', "f" },
	{ "__builtin_log1p", "log1p", 'd', "d" }, { "__builtin_log1pf", "log1pf", 'f', "f" },
	{ "__builtin_pow", "pow", 'd', "dd" }, { "__builtin_powf", "powf", 'f', "ff" },
	{ "__builtin_fmod", "fmod", 'd', "dd" }, { "__builtin_fmodf", "fmodf", 'f', "ff" },
	{ "__builtin_atan2", "atan2", 'd', "dd" }, { "__builtin_atan2f", "atan2f", 'f', "ff" },
	{ "__builtin_hypot", "hypot", 'd', "dd" }, { "__builtin_hypotf", "hypotf", 'f', "ff" },
	{ "__builtin_copysign", "copysign", 'd', "dd" }, { "__builtin_copysignf", "copysignf", 'f', "ff" },
	{ "__builtin_fmin", "fmin", 'd', "dd" }, { "__builtin_fminf", "fminf", 'f', "ff" },
	{ "__builtin_fmax", "fmax", 'd', "dd" }, { "__builtin_fmaxf", "fmaxf", 'f', "ff" },
	{ "__builtin_nan", "nan", 'd', "s" }, { "__builtin_nanf", "nanf", 'f', "s" },
	{ "__builtin_char_memchr", "memchr", 'p', "qiz" }, { "__builtin_strchr", "strchr", 'p', "si" },
	{ "__builtin_strncmp", "strncmp", 'i', "ssz" }, { "__builtin_bcmp", "memcmp", 'i', "qqz" },
	{ "__builtin_free", "free", 'v', "p" }, { "__builtin_malloc", "malloc", 'p', "z" },
};

static qty cxx_builtin_code_type(cxx *c, char code)
{
	cc *a = c->ast;

	switch (code) {
	case 'p': return cc_type_pointer(a, cc_type_basic(TY_VOID));
	case 'q': return cc_type_pointer(a, cc_type_qualify(a, cc_type_basic(TY_VOID), Q_CONST));
	case 's': return cc_type_pointer(a, cc_type_qualify(a, cc_type_basic(TY_CHAR), Q_CONST));
	case 'z': return cc_type_basic(TY_ULONG);
	case 'i': return cc_type_basic(TY_INT);
	case 'f': return cc_type_basic(TY_FLOAT);
	case 'd': return cc_type_basic(TY_DOUBLE);
	}
	return cc_type_basic(TY_VOID);
}

static const struct cxx_builtin_function *cxx_builtin_function_find(const char *name, size_t len, uint32_t nargs, size_t *index)
{
	for (size_t i = 0; i < sizeof cxx_builtin_functions / sizeof *cxx_builtin_functions; i++)
		if (strlen(cxx_builtin_functions[i].name) == len && !memcmp(cxx_builtin_functions[i].name, name, len) &&
		    strlen(cxx_builtin_functions[i].params) == nargs) {
			*index = i;
			return &cxx_builtin_functions[i];
		}
	return NULL;
}

/* __builtin_isnan, isinf, isfinite, isnormal: 1 2 3 4 (0: not one) */
static int cxx_float_class_builtin(const char *name, size_t len)
{
	if (len == 15 && !memcmp(name, "__builtin_isnan", 15)) return 1;
	if (len == 15 && !memcmp(name, "__builtin_isinf", 15)) return 2;
	if (len == 18 && !memcmp(name, "__builtin_isfinite", 18)) return 3;
	if (len == 18 && !memcmp(name, "__builtin_isnormal", 18)) return 4;
	return 0;
}

/* a builtin call's type, for the builtins vcc knows as functions or simple operations (arg0: the first argument's type) */
VPRIV bool cxx_builtin_function_type(cxx *c, const char *name, size_t len, uint32_t nargs, qty arg0, qty *ret)
{
	const struct cxx_builtin_function *b;
	size_t i;

	if ((b = cxx_builtin_function_find(name, len, nargs, &i))) {
		*ret = cxx_builtin_code_type(c, b->ret);
		return true;
	}
	if (len == 31 && !memcmp(name, "__builtin_is_constant_evaluated", 31) && nargs == 0) {
		*ret = cc_type_basic(TY_BOOL);
		return true;
	}
	if (len == 16 && !memcmp(name, "__builtin_expect", 16) && nargs == 2 && arg0 != CXX_TY_UNKNOWN) {
		*ret = cc_type_basic(TY_LONG);
		return true;
	}
	if (len == 20 && !memcmp(name, "__builtin_constant_p", 20) && nargs == 1) {
		*ret = cc_type_basic(TY_INT);
		return true;
	}
	if (nargs == 1 && arg0 != CXX_TY_UNKNOWN && cxx_float_class_builtin(name, len) &&
	    (AST_TYPE_KIND(c->ast, arg0) == TY_FLOAT || AST_TYPE_KIND(c->ast, arg0) == TY_DOUBLE || AST_TYPE_KIND(c->ast, arg0) == TY_LDOUBLE)) {
		*ret = cc_type_basic(TY_INT);
		return true;
	}
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19) && nargs == 1 && arg0 != CXX_TY_UNKNOWN) {
		*ret = cc_type_pointer(c->ast, arg0);
		return true;
	}
	if (len == 24 && !memcmp(name, "__builtin_assume_aligned", 24) && (nargs == 2 || nargs == 3)) {
		*ret = cc_type_pointer(c->ast, cc_type_basic(TY_VOID));
		return true;
	}
	return false;
}

/* ... and its code: a call of the library function, or the operation */
VPRIV void cxx_gen_builtin(cxx *c, const char *name, size_t len, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const struct cxx_builtin_function *b;
	size_t i;

	if (!cxx_gen_on(c))
		return;
	if (len == 31 && !memcmp(name, "__builtin_is_constant_evaluated", 31)) {
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(TY_BOOL), loc);	/* (code that runs: not a constant evaluation) */
		return;
	}
	if (len == 16 && !memcmp(name, "__builtin_expect", 16)) {
		if (args[0])
			c->enode = cc_conversion_apply(a, cc_conversion_decay(a, args[0]), cc_type_basic(TY_LONG));
		return;
	}
	if (len == 20 && !memcmp(name, "__builtin_constant_p", 20)) {
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);	/* (not folded here: never a constant) */
		return;
	}
	if (cxx_float_class_builtin(name, len) && nargs == 1 && args[0]) {
		/* by comparisons: NaN is not equal to itself; x - x is NaN for an infinity or a NaN, else 0 */
		int k = cxx_float_class_builtin(name, len);
		qty ft;
		uint32_t (*x)(cc *, uint32_t) = cc_node_rvalue;
		uint32_t v, r;

		v = x(a, args[0]);
		ft = QUNQ(AST_NODE_AT(a, v)->ty);
		if (AST_TYPE_KIND(a, ft) != TY_FLOAT && AST_TYPE_KIND(a, ft) != TY_DOUBLE && AST_TYPE_KIND(a, ft) != TY_LDOUBLE)
			return;
		if (k == 4 && AST_TYPE_KIND(a, ft) == TY_LDOUBLE)
			return;	/* (its smallest normal: not a double) */
#define ARG() x(a, args[0])
#define DIFF() cc_binary_build(a, LEX_TOKEN_MINUS, ARG(), ARG(), loc)
		if (k == 1)
			r = cc_binary_build(a, LEX_TOKEN_EXCLAMATION_EQUAL, ARG(), ARG(), loc);
		else if (k == 2)
			r = cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, ARG(), ARG(), loc),
					    cc_binary_build(a, LEX_TOKEN_EXCLAMATION_EQUAL, DIFF(), DIFF(), loc), loc);
		else {
			r = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, DIFF(), DIFF(), loc);
			if (k == 4) {
				/* finite and |x| >= the smallest normal */
				double mn = AST_TYPE_KIND(a, ft) == TY_FLOAT ? 0x1p-126 : 0x1p-1022;
				uint32_t lo = cc_node_float_new(a, mn, ft, loc), nlo = cc_node_float_new(a, -mn, ft, loc);

				r = cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, r,
						    cc_binary_build(a, LEX_TOKEN_PIPE_PIPE, cc_binary_build(a, LEX_TOKEN_GREATER_EQUAL, ARG(), lo, loc),
								    cc_binary_build(a, LEX_TOKEN_LESS_EQUAL, ARG(), nlo, loc), loc), loc);
			}
		}
#undef ARG
#undef DIFF
		c->enode = cc_conversion_apply(a, r, cc_type_basic(TY_INT));
		return;
	}
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19)) {
		if (args[0] && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE))
			c->enode = cc_address_of_build(a, args[0], loc);
		return;
	}
	if (len == 24 && !memcmp(name, "__builtin_assume_aligned", 24)) {
		/* the pointer itself, as void * (the alignment it promises: not used at -O0) */
		uint32_t p = args[0] ? cc_conversion_decay(a, args[0]) : 0;

		if (p && AST_TYPE_KIND(a, AST_NODE_AT(a, p)->ty) == TY_PTR)
			c->enode = cc_cast_insert(a, p, cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP);
		return;
	}
	if (!(b = cxx_builtin_function_find(name, len, nargs, &i)) || i >= 128)
		return;
	if (!c->gen_builtin_syms[i]) {
		qty ps[4];
		uint32_t s;

		for (uint32_t k = 0; k < nargs; k++)
			ps[k] = cxx_builtin_code_type(c, b->params[k]);
		s = cc_symbol_new(a, S_FUNC, vintern_cstr(b->symbol), cxx_type_function(c, cxx_builtin_code_type(c, b->ret), ps, nargs, false), loc);
		a->syms[s].flags |= SF_GLOBAL;
		a->syms[s].depth = 0;
		c->gen_builtin_syms[i] = s;
	}
	{
		cc_id_list list;
		uint32_t s = c->gen_builtin_syms[i], fn, n;

		cc_id_list_init(&list);
		for (uint32_t k = 0; k < nargs; k++) {
			if (!args[k])
				return;
			cc_id_list_append(a, &list, cc_conversion_apply(a, cc_conversion_decay(a, args[k]), cxx_builtin_code_type(c, b->params[k])));
		}
		fn = cc_node_rvalue(a, cxx_gen_var_node(c, s, loc));
		n = cc_node_new(a, N_CALL, cxx_builtin_code_type(c, b->ret), loc);
		AST_NODE_AT(a, n)->a = fn;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
		AST_NODE_AT(a, n)->c = nargs;
		c->enode = n;
	}
}

/* ---- constructors and destructors ------------------------------------------------------------ */

static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t n = cc_node_new(c->ast, N_VAR, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, n)->a = s;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	return n;
}

/* does the class declare a constructor, or do its bases or members of class type need one called? */
static bool cxx_gen_needs_construction(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
		return false;
	if (!cls || depth > 8 || cxx_class_polymorphic(c, t) || cxx_class_has_member_initializer(c, t))
		return true;	/* (a vptr to set, members' default initializers) */
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_needs_construction(c, bases[i], depth + 1))
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) && cxx_gen_needs_construction(c, ft, depth + 1))
			return true;
	}
	return false;
}

/* a destructor about to be parsed: the destructors its end runs (its members' and bases' classes) have
   their bodies parsed first, not from inside its code generation */
VPRIV void cxx_gen_destructor_parts_use(cxx *c, cxx_entity *d)
{
	cxx_scope *cls = d ? d->member_of : NULL;
	qty bases[16];
	uint32_t nb;

	if (!cls || !cls->owner || cls->owner->ty == CXX_TY_UNKNOWN)
		return;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == TY_ARRAY)
			ft = AST_TYPE(c->ast, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, ft)) {
			cxx_entity *md = cxx_gen_declared_destructor(c, ft);

			if (md && md != d)
				cxx_member_body_use(c, md);
		}
	}
	nb = cxx_class_bases(c, cls->owner->ty, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		cxx_entity *bd = cxx_gen_declared_destructor(c, bases[i]);

		if (bd && bd != d)
			cxx_member_body_use(c, bd);
	}
}

/* X::~X() = default; outside its class: its body made now, from the class's members and bases */
VPRIV void cxx_gen_defaulted_destructor(cxx *c, cxx_entity *d)
{
	if (d && d->member_of && d->member_of->owner && d->member_of->owner->ty != CXX_TY_UNKNOWN) {
		d->fn_ooc_default = 1, d->fn_inline = 0;
		cxx_gen_destructor_body(c, d->member_of->owner->ty, d);
	}
}

/* the destructor the class declares (NULL: none) */
static cxx_entity *cxx_gen_declared_destructor(cxx *c, qty t)
{
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	cxx_entity *d = cls ? cxx_lookup_in(c, cls, cxx_dtor_id(), 0) : NULL;

	return d && d->member_of == cls && d->kind == CXX_ENTITY_VALUE ? d : NULL;
}

/* does destroying an object of the class do anything: a declared destructor, here or in a base or member? */
static bool cxx_gen_needs_destruction(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	bool pod, empty;

	if (cxx_class_layout_info(c, t, &pod, &empty) && pod)
		return false;
	if (!cls || depth > 8)
		return true;
	if (cxx_gen_declared_destructor(c, t))
		return true;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (cxx_gen_needs_destruction(c, bases[i], depth + 1))
			return true;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft) && cxx_gen_needs_destruction(c, ft, depth + 1))
			return true;
	}
	return false;
}

/*
 * An object of class type t at the lvalue `target` is initialized from args: the call of the constructor
 * cxx_ctor_use chose just before, or a copy from an object of the class where none is declared for that,
 * or nothing for a class that needs no construction.  The statements go to `out`.  False: not built yet.
 */
static bool cxx_gen_default_construct(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
static bool cxx_gen_default_construct_parts(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth);
/*
 * T{a, b} or T(a, b) of an aggregate (a class with no constructor, no base, no virtual function): zeroed,
 * then its members in order from the elements; those after the last element keep their zero, or get their
 * default initializer.  False: not built (anonymous members, arrays from lists, members of classes with
 * constructors from more than their element).
 */
static bool cxx_gen_aggregate_init(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	const tag *r;
	qty bases[2];
	uint32_t k = 0, zargs[3], saved = c->enode, zero;

	if (!cls || AST_TYPE_KIND(a, QUNQ(t)) != TY_STRUCT || cxx_class_polymorphic(c, t) || cxx_class_bases(c, t, bases, 2))
		return false;
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return false;
	r = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	for (uint32_t i = 0; i < r->nfield; i++)
		if (!r->f[i].name)
			return false;
	if (AST_NODE_AT(a, target)->ty != QUNQ(AST_NODE_AT(a, target)->ty)) {
		qty pt = cc_type_pointer(a, QUNQ(t));
		uint32_t addr = cc_address_of_build(a, target, loc);

		target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
	}
	zargs[0] = cc_address_of_build(a, target, loc);
	zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
	zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	c->enode = 0;
	cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
	zero = c->enode;
	c->enode = saved;
	if (!zero)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		uint32_t m = member(a, target, r->f[i].name, loc);
		cxx_entity *fe = cxx_lookup_local(cls, r->f[i].name);

		while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
			fe = fe->next;
		if (m && AST_TYPE_KIND(a, ft) == TY_ARRAY && AST_TYPE(a, ft)->n && cxx_gen_type_ok(c, AST_TYPE(a, ft)->base) &&
		    !cxx_gen_is_reference(c, AST_TYPE(a, ft)->base)) {
			/* an array of scalars: the next elements, braces elided (std::array<int, 3> a{1, 2, 3}); the rest keep their
			   zero */
			qty et = AST_TYPE(a, ft)->base;

			if (fe && fe->has_nsdmi && k >= nargs)
				return false;
			for (uint64_t j = 0; j < AST_TYPE(a, ft)->n && k < nargs; j++, k++) {
				uint32_t el, v;

				if (!args[k] || cc_type_is_record(a, AST_NODE_AT(a, args[k])->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, args[k])->ty) == TY_ARRAY)
					return false;
				el = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, m),
								cc_node_integer_new(a, (i128)j, cc_type_basic(TY_LONG), loc), loc), loc);
				if (et != QUNQ(et)) {
					qty pt = cc_type_pointer(a, QUNQ(et));
					uint32_t addr = cc_address_of_build(a, el, loc);

					el = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
				}
				if (!(v = cxx_gen_bind(c, args[k], QUNQ(et), loc)))
					return false;
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, el, v, loc), 0, 0));
			}
			continue;
		}
		if (!m || AST_TYPE_KIND(a, ft) == TY_ARRAY)
			return false;
		if (k >= nargs) {
			if (fe && fe->has_nsdmi)
				return false;	/* (a default initializer after the elements: needs this, not built yet) */
			if (cc_type_is_record(a, ft) && cxx_gen_needs_construction(c, ft, 0) && !cxx_gen_default_construct(c, m, ft, out, loc, 1))
				return false;
			continue;
		}
		if (!args[k])
			return false;
		if (cc_type_is_record(a, ft)) {
			qty at[1] = { AST_NODE_AT(a, args[k])->ty };
			uint8_t lv[1] = { (AST_NODE_AT(a, args[k])->flags & NF_LVALUE) != 0 };

			cxx_ctor_use(c, QUNQ(ft), at, lv, 1, false);
			if (!cxx_gen_construct_at(c, m, QUNQ(ft), &args[k], 1, out, loc))
				return false;
		} else {
			uint32_t v = cxx_gen_bind(c, args[k], ft, loc);

			if (!v)
				return false;
			if (cxx_gen_is_reference(c, ft))
				return false;	/* (a reference member: not built yet) */
			if (ft != QUNQ(ft)) {
				qty pt = cc_type_pointer(a, QUNQ(ft));
				uint32_t addr = cc_address_of_build(a, m, loc);

				m = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
			}
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, m, v, loc), 0, 0));
		}
		k++;
	}
	if (k != nargs)
		return false;	/* (more elements than the members take) */
	return true;
}

/*
 * A std::initializer_list<E> for a braced list's elements (the arguments of T{...}): a local array of them,
 * converted to E (constructed, for a class E), and a local initializer_list pointing at it, as an lvalue
 * (0: not built: libc++'s or libstdc++'s members not found; a class E's are destroyed by the caller).
 */
static bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc);

static uint32_t cxx_gen_initializer_list(cxx *c, qty ilt, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc, uint32_t *array)
{
	cc *a = c->ast;
	qty lt = cc_type_basic(TY_LONG), et, pt;
	uint32_t arr = 0, il, begin, size;
	cc_id_list *saved = c->gen_block;
	qty types[16];
	uint8_t lvals[16];
	uint32_t types_n = c->ctor_ilist_n == nargs ? nargs : 0;	/* (the element constructors' own uses overwrite them) */

	uint32_t braces[16];

	memcpy(types, c->ctor_ilist_types, sizeof types), memcpy(lvals, c->ctor_ilist_lval, sizeof lvals);
	memcpy(braces, c->ctor_ilist_brace, sizeof braces);
	if (!types_n)
		memset(braces, 0, sizeof braces);
	if (!cxx_tag_scope_get(c, QUNQ(ilt)) || !(il = cxx_gen_local(c, 0, QUNQ(ilt), loc)))
		return 0;
	begin = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("__begin_"), loc, 0);
	size = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("__size_"), loc, 0);
	if (!begin || !size) {	/* (libstdc++'s) */
		begin = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("_M_array"), loc, 0);
		size = cxx_gen_member_of(c, cxx_gen_var_node(c, il, loc), vintern_cstr("_M_len"), loc, 0);
	}
	if (!begin || !size || AST_TYPE_KIND(a, AST_NODE_AT(a, begin)->ty) != TY_PTR)
		return 0;
	pt = AST_NODE_AT(a, begin)->ty, et = QUNQ(AST_TYPE(a, pt)->base);
	if (!cxx_gen_object_type_ok(c, et))
		return 0;
	c->gen_block = out;
	if (nargs) {
		if (!(arr = cxx_gen_local(c, 0, cc_type_array(a, et, nargs, 0), loc)))
			goto fail;
		cxx_gen_local_declare(c, arr, 0, false, loc);
	}
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
								  cc_node_integer_new(a, i, lt, loc), loc), loc);

		if (!args[i] && i < 16 && braces[i] && cc_type_is_record(a, et)) {
			/* the element is a braced list itself, {"a", 1}: E{...} from its arguments, parsed again here for
			   their code (its first parse was unevaluated) */
			uint32_t pos = c->pos, an[16], n;
			qty at[16];
			uint8_t al[16];

			c->pos = braces[i];
			cxx_braced_arguments_parse(c);
			c->pos = pos;
			n = c->ncargs;
			if (n == UINT32_MAX || n > 16)
				goto fail;
			memcpy(an, c->carg_nodes, sizeof an), memcpy(at, c->cargs, n * sizeof *at), memcpy(al, c->cargs_lval, n);
			cxx_ctor_use(c, et, at, al, n, true);
			if (!cxx_gen_construct_at(c, slot, et, an, n, out, loc))
				goto fail;
			continue;
		}
		if (!args[i])
			goto fail;
		if (cc_type_is_record(a, et)) {
			qty at = i < types_n && types[i] != CXX_TY_UNKNOWN ? types[i] : AST_NODE_AT(a, args[i])->ty;
			uint8_t lv = i < types_n ? lvals[i] : (AST_NODE_AT(a, args[i])->flags & NF_LVALUE) != 0;
			uint32_t x = args[i];

			if (c->nullptr_ty && QUNQ(at) == c->nullptr_ty && AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_PTR) {
				/* nullptr (its node a void *): a std::nullptr_t object, which a T&& parameter binds */
				uint32_t np = cxx_gen_local(c, 0, c->nullptr_ty, loc);

				if (!np)
					goto fail;
				cxx_gen_local_declare(c, np, 0, false, loc);
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0,
					cc_dereference_build(a, cc_cast_insert(a, cc_address_of_build(a, cxx_gen_var_node(c, np, loc), loc),
						cc_type_pointer(a, AST_NODE_AT(a, x)->ty), CK_P2P), loc), x, loc), 0, 0));
				x = cxx_gen_var_node(c, np, loc);
			}

			cxx_ctor_use(c, et, &at, &lv, 1, false);
			if (!cxx_gen_construct_at(c, slot, et, &x, 1, out, loc))
				goto fail;
		} else {
			uint32_t x = cxx_gen_bind(c, args[i], et, loc);

			if (!x)
				goto fail;
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, slot, x, loc), 0, 0));
		}
	}
	cxx_gen_local_declare(c, il, 0, false, loc);
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, begin,
		nargs ? cc_conversion_apply(a, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)), pt) : cc_conversion_apply(a, cc_node_integer_new(a, 0, lt, loc), pt), loc), 0, 0));
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, size,
		cc_conversion_apply(a, cc_node_integer_new(a, nargs, lt, loc), AST_NODE_AT(a, size)->ty), loc), 0, 0));
	c->gen_block = saved;
	*array = arr;
	return cxx_gen_var_node(c, il, loc);
fail:
	c->gen_block = saved;
	return 0;
}

static bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *ctor = c->ctor_picked;
	bool found = c->ctor_found;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));

	if (!target || !cls || nargs == UINT32_MAX || nargs > 16)
		return false;
	if (c->ctor_ilist && c->ctor_ilist->member_of == cls && c->ctor_ilist->ty != CXX_TY_UNKNOWN) {
		/* T{a, b} with an initializer_list constructor: it, from the list (its other parameters defaulted) */
		cxx_entity *ilc = c->ctor_ilist;
		uint32_t il, arr = 0;
		qty et;

		c->ctor_ilist = NULL;
		il = cxx_gen_initializer_list(c, a->params[AST_TYPE(a, ilc->ty)->aux], args, nargs, out, loc, &arr);
		if (!il)
			return false;
		cxx_gen_call(c, ilc, target, false, &il, 1, loc);
		if (!c->enode)
			return false;
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		/* the array's elements live to the end of the full-expression: here, the declaration's (not for an
		   exception thrown by the constructor) */
		et = arr ? AST_TYPE(a, a->syms[arr].ty)->base : CXX_TY_UNKNOWN;
		if (arr && cc_type_is_record(a, et) && cxx_gen_needs_destruction(c, et, 0))
			for (uint32_t i = nargs; i-- > 0;) {
				uint32_t slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
									  cc_node_integer_new(a, i, cc_type_basic(TY_LONG), loc), loc), loc);

				if (!cxx_gen_destroy(c, slot, et, out, loc))
					return false;
			}
		return true;
	}
	if (ctor && ctor->member_of != cls)
		ctor = NULL, found = false;	/* (a base's constructor, found by name: not this class's) */
	if (ctor && nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)) {
		/* from an object of the class: only a copy or move constructor takes it (not one found through a
		   conversion of the object to something else) */
		const ctype *cf = ctor->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ctor->ty) == TY_FUNC ? AST_TYPE(a, ctor->ty) : NULL;
		qty p0 = cf && cf->n ? a->params[cf->aux] : CXX_TY_UNKNOWN;

		if (p0 == CXX_TY_UNKNOWN || !(QUNQ(p0) == QUNQ(t) || (cxx_gen_is_reference(c, p0) && QUNQ(AST_TYPE(a, p0)->base) == QUNQ(t))))
			ctor = NULL, found = false;
	}
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)) {
		/* from a prvalue of the class: constructed here in the first place */
		cxx_prv *p = cxx_gen_prv_find(c, args[0]);

		if (p && p->call) {
			cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cxx_gen_prv_aim(c, p, cc_address_of_build(a, target, loc)), 0, 0));
			return true;
		}
	}
	if (found && ctor && ctor->fn_defaulted && nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) &&
	    cxx_gen_defaulted_copy_trivial(c, t)) {
		/* T(const T&) = default, T(T&&) = default of a trivially copyable class: its bytes, as clang does (it
		   calls no function, and defines none) */
		ctor = NULL, found = false;
		goto bitwise;
	}
	if (found && ctor && ctor->fn_defaulted && !nargs) {	/* T() = default: what the implicit one does, in place */
		if (cxx_class_has_member_initializer(c, t))
			return cxx_gen_construct_by_body(c, target, t, ctor, out, loc);
		return cxx_gen_default_construct_parts(c, target, t, out, loc, 0);
	}
	if (found && ctor) {
		cxx_gen_call(c, ctor, target, false, args, nargs, loc);
		if (!c->enode)
			return false;
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		return true;
	}
	if (nargs == 0)
		return cxx_gen_default_construct(c, target, t, out, loc, 0);	/* (the implicit default constructor, in place) */
bitwise:
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) && (cxx_gen_type_ok(c, t) || cxx_gen_trivial_abi(c, t, 0) == 1)) {
		/* from an object of a class C copies as C++ does (a const object is initialized all the same) */
		bool pod, empty;

		if (cxx_class_layout_info(c, t, &pod, &empty) && empty) {
			/* an empty class: nothing to copy (its byte may be another member's, [[no_unique_address]]
			   __value_comp_ at __size_'s offset in std::__tree) */
			if (!(AST_NODE_AT(a, args[0])->flags & NF_LVALUE))
				cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, args[0], 0, 0));
			return true;
		}
		if (AST_NODE_AT(a, target)->ty != QUNQ(AST_NODE_AT(a, target)->ty)) {
			qty pt = cc_type_pointer(a, QUNQ(t));
			uint32_t addr = cc_address_of_build(a, target, loc);

			target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
		}
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, cc_node_rvalue(a, args[0]), loc), 0, 0));
		return true;
	}
	if (!found && !(nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t)))
		return cxx_gen_aggregate_init(c, target, t, args, nargs, out, loc);
	return false;	/* (an implicit copy of a class with constructors: not built yet) */
}

/* this->... : the object being constructed or destroyed, as an lvalue */
static uint32_t cxx_gen_this_object(cxx *c, uint32_t loc)
{
	uint32_t p;

	cxx_gen_this(c, loc);
	p = c->enode;
	c->enode = 0;
	return p ? cc_dereference_build(c->ast, p, loc) : 0;
}

/*
 * A constructor's initializer: name(args) for a member, or a base class (class_type; this class itself:
 * a delegating constructor).  A scalar member is assigned (zero for no argument); a member or base of
 * class type is constructed (cxx_ctor_use was called for it with these arguments).
 */
VPRIV void cxx_gen_member_initializer(cxx *c, uint32_t name, qty class_type, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self, target, value;
	qty mt;

	if (!cxx_gen_on(c) || !c->gen_this || !c->gen_pre)
		return;
	self = cxx_gen_this_object(c, loc);
	if (!self || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	if (!name) {
		/* a base class, or the class itself */
		qty own = AST_NODE_AT(a, self)->ty;

		if (class_type == CXX_TY_UNKNOWN || !cc_type_is_record(a, class_type)) {
			cxx_gen_fail(c);
			return;
		}
		if (QUNQ(class_type) == QUNQ(own))
			target = self, c->gen_delegates = 1;
		else
			target = cxx_gen_base_object(c, self, QUNQ(class_type), loc), c->gen_base_type = QUNQ(class_type);
		if (!target || !cxx_gen_construct_at(c, target, QUNQ(class_type), args, nargs, c->gen_pre, loc)) {
			c->gen_base_type = 0;
			cxx_gen_fail(c);
			return;
		}
		c->gen_base_type = 0;
		if (c->ngen_init_bases < 16)
			c->gen_init_bases[c->ngen_init_bases++] = QUNQ(class_type);
		return;
	}
	{
		/* the member as it is in the object (through anonymous structs and unions too) */
		uint32_t raw = cxx_gen_member_of(c, self, name, loc, 0);

		if (raw && cxx_gen_is_reference(c, AST_NODE_AT(a, raw)->ty)) {
			/* a reference member: bound to its initializer (the member holds the address) */
			uint32_t v = nargs == 1 && args[0] ? cxx_gen_bind(c, args[0], AST_NODE_AT(a, raw)->ty, loc) : 0;

			if (!v) {
				cxx_gen_fail(c);
				return;
			}
			if (c->ngen_init_names < 32)
				c->gen_init_names[c->ngen_init_names++] = name;
			cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, raw, v, loc), 0, 0));
			return;
		}
		if (!raw) {
			/* not in the record: an empty [[no_unique_address]] member, which takes no space (an allocator, a
			   comparator): constructed all the same, at the object's own address */
			cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(AST_NODE_AT(a, self)->ty));
			cxx_entity *fe = cls ? cxx_lookup_local(cls, name) : NULL;
			bool pod, empty;

			if (fe && fe->kind == CXX_ENTITY_VALUE && fe->field_of == cls && fe->ty != CXX_TY_UNKNOWN && cc_type_is_record(a, fe->ty) &&
			    AST_TYPE_KIND(a, fe->ty) != TY_ARRAY && cxx_class_layout_info(c, fe->ty, &pod, &empty) && empty) {
				qty pt = cc_type_pointer(a, QUNQ(fe->ty));
				uint32_t addr = cc_address_of_build(a, self, loc);

				target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
				if (c->ngen_init_names < 32)
					c->gen_init_names[c->ngen_init_names++] = name;
				if (!cxx_gen_construct_at(c, target, QUNQ(fe->ty), args, nargs, c->gen_pre, loc))
					cxx_gen_fail(c);
				return;
			}
			cxx_gen_fail(c);	/* (a static member, a member of a class not laid out) */
			return;
		}
	}
	target = cxx_gen_member_of(c, self, name, loc, 0);
	if (!target || !(AST_NODE_AT(a, target)->flags & NF_LVALUE)) {
		cxx_gen_fail(c);
		return;
	}
	if (c->ngen_init_names < 32)
		c->gen_init_names[c->ngen_init_names++] = name;
	mt = AST_NODE_AT(a, target)->ty;
	if (cc_type_is_record(a, mt) && AST_TYPE_KIND(a, mt) != TY_ARRAY) {
		bool own = c->ctor_found && c->ctor_picked && c->ctor_picked->member_of == cxx_tag_scope_get(c, QUNQ(mt));

		if (!nargs && (!own || c->ctor_picked->fn_defaulted)) {
			/* member(): value-initialized; without a user-provided default constructor zeroed first (a union
			   with T() = default), then default-constructed if anything needs it */
			uint32_t zargs[3], saved = c->enode, zero;
			cxx_entity *picked = c->ctor_picked;
			uint8_t found = c->ctor_found, none = c->ctor_none;

			zargs[0] = cc_address_of_build(a, target, loc);
			zargs[1] = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);
			zargs[2] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(mt))->size, cc_type_basic(TY_ULONG), loc);
			c->enode = 0;
			cxx_gen_builtin(c, "__builtin_memset", 16, zargs, 3, loc);
			zero = c->enode;
			c->enode = saved;
			if (!zero) {
				cxx_gen_fail(c);
				return;
			}
			cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, zero, 0, 0));
			c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
			if (cxx_gen_needs_construction(c, QUNQ(mt), 0) && !cxx_gen_construct_at(c, target, QUNQ(mt), args, nargs, c->gen_pre, loc))
				cxx_gen_fail(c);
			return;
		}
		if (!cxx_gen_construct_at(c, target, QUNQ(mt), args, nargs, c->gen_pre, loc))
			cxx_gen_fail(c);
		return;
	}
	if (nargs > 1 || (nargs == 1 && !args[0]) || AST_TYPE_KIND(a, mt) == TY_ARRAY || !cxx_gen_type_ok(c, mt)) {
		cxx_gen_fail(c);
		return;
	}
	value = nargs ? cc_conversion_apply(a, cc_conversion_decay(a, args[0]), QUNQ(mt)) :
			cc_conversion_apply(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), QUNQ(mt));
	if (mt != QUNQ(mt)) {
		/* a const member: initialized all the same (through a pointer to its unqualified type) */
		qty pt = cc_type_pointer(a, QUNQ(mt));
		uint32_t addr = cc_address_of_build(a, target, loc);

		target = cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
	}
	cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, value, loc), 0, 0));
}

/*
 * A constructor's body is done: the bases and members of class type its initializers did not name are
 * default-constructed first (into `first`).  False: one of them is not built yet.
 */
/* ... the members of the record r (the class's, or an anonymous struct's inside it) that the initializers
   did not name and that need construction */
/*
 * A data member's default initializer, applied by a constructor whose initializers do not name the member:
 * parsed again where it was written (as a default argument is), into `first`.  False: not built.
 */
static bool cxx_gen_nsdmi(cxx *c, cxx_entity *fe, cc_id_list *first, uint32_t loc)
{
	jmp_buf jb, *outer_fail = c->fail;
	cxx_scope *scope = c->scope;
	uint32_t pos = c->pos, enode = c->enode, eobj = c->eobj_node;
	qty etype = c->etype;
	uint8_t elval = c->elval, econst = c->econst, einvalid = c->einvalid, call_resolved = c->call_resolved, none_viable = c->none_viable;
	uint8_t eobj_arrow = c->eobj_arrow, eunsure = c->eunsure;
	int64_t evalue = c->evalue;
	cxx_entity *call_picked = c->call_picked, *ecallee = c->ecallee;
	void *pre = c->gen_pre;
	volatile bool ok = false;

	if (!fe->nsdmi_pos || !fe->nsdmi_scope || fe->ty == CXX_TY_UNKNOWN)
		return false;
	c->pos = fe->nsdmi_pos, c->scope = fe->nsdmi_scope, c->fail = &jb;
	c->gen_pre = first;
	if (!setjmp(jb)) {
		uint32_t an[16], n = 0;
		qty at[16];
		uint8_t lv[16];
		bool braced = cxx_is(c, LEX_TOKEN_LEFT_BRACE) || (cxx_is(c, LEX_TOKEN_EQUAL) && cxx_peek(c, 1)->kind == LEX_TOKEN_LEFT_BRACE);
		bool record = cc_type_is_record(c->ast, fe->ty) && AST_TYPE_KIND(c->ast, fe->ty) != TY_ARRAY;

		cxx_accept(c, LEX_TOKEN_EQUAL);
		if (braced) {
			cxx_braced_arguments_parse(c);
			n = c->ncargs;
			if (n != UINT32_MAX && n <= 16) {
				memcpy(an, c->carg_nodes, n * sizeof *an);
				memcpy(at, c->cargs, n * sizeof *at);
				memcpy(lv, c->cargs_lval, n * sizeof *lv);
			}
		} else {
			cxx_assignment_expression_parse(c);
			an[0] = c->enode, at[0] = c->etype, lv[0] = c->elval, n = 1;
			if (at[0] != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, at[0]) == TY_ARRAY)	/* ("text": a pointer) */
				at[0] = cc_type_pointer(c->ast, AST_TYPE(c->ast, at[0])->base), lv[0] = 0;
		}
		if (n != UINT32_MAX && n <= 16) {
			if (record)
				cxx_ctor_use(c, QUNQ(fe->ty), at, lv, n, braced);
			cxx_gen_member_initializer(c, fe->name, CXX_TY_UNKNOWN, an, n, loc);
			ok = cxx_gen_on(c);
		}
	}
	c->gen_pre = pre;
	c->fail = outer_fail, c->pos = pos, c->scope = scope;
	c->enode = enode, c->eobj_node = eobj, c->eobj_arrow = eobj_arrow, c->etype = etype, c->elval = elval, c->econst = econst;
	c->evalue = evalue, c->einvalid = einvalid, c->eunsure = eunsure, c->call_resolved = call_resolved, c->none_viable = none_viable;
	c->call_picked = call_picked, c->ecallee = ecallee;
	return ok;
}

static bool cxx_gen_constructor_member_defaults(cxx *c, cxx_scope *cls, const tag *r, cc_id_list *first, uint32_t loc, int depth)
{
	cc *a = c->ast;

	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		bool named = false;

		if (r->f[i].name) {
			/* a member with a default initializer: it, unless an initializer names the member */
			cxx_entity *fe = cxx_lookup_local(cls, r->f[i].name);

			while (fe && !(fe->kind == CXX_ENTITY_VALUE && fe->field_of))
				fe = fe->next;
			if (fe && fe->has_nsdmi) {
				for (uint32_t k = 0; k < c->ngen_init_names; k++)
					named |= c->gen_init_names[k] == r->f[i].name;
				if (named)
					continue;
				if (!cxx_gen_nsdmi(c, fe, first, loc))
					return false;
				continue;
			}
		}

		if (!r->f[i].name && cc_type_is_record(a, ft) && AST_TYPE_KIND(a, ft) != TY_ARRAY && depth < 4) {
			/* an anonymous struct: its members are the class's (a union: none is made active) */
			if (AST_TYPE_KIND(a, ft) == TY_STRUCT &&
			    !cxx_gen_constructor_member_defaults(c, cls, &a->tags[AST_TYPE(a, ft)->aux], first, loc, depth + 1))
				return false;
			continue;
		}
		if (!r->f[i].name || !cc_type_is_record(a, ft) || AST_TYPE_KIND(a, ft) == TY_ARRAY) {
			/* (an array of a class with a constructor: not built yet) */
			qty et = ft;

			while (AST_TYPE_KIND(a, et) == TY_ARRAY)
				et = AST_TYPE(a, et)->base;
			if (cc_type_is_record(a, et) && cxx_gen_needs_construction(c, et, 0))
				return false;
			continue;
		}
		for (uint32_t k = 0; k < c->ngen_init_names; k++)
			named |= c->gen_init_names[k] == r->f[i].name;
		if (named || !cxx_gen_needs_construction(c, ft, 0))
			continue;
		cxx_ctor_use(c, ft, NULL, NULL, 0, false);
		if (!cxx_gen_construct_at(c, member(a, cxx_gen_this_object(c, loc), r->f[i].name, loc), QUNQ(ft), NULL, 0, first, loc))
			return false;
	}
	return true;
}

static bool cxx_gen_constructor_defaults(cxx *c, cc_id_list *first, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self = cxx_gen_this_object(c, loc);
	qty own, bases[16];
	uint32_t nb;

	if (!self)
		return false;
	if (c->gen_delegates)
		return true;	/* (the constructor delegated to did it all) */
	own = AST_NODE_AT(a, self)->ty;
	nb = cxx_class_bases(c, own, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		bool named = false;

		for (uint32_t k = 0; k < c->ngen_init_bases; k++)
			named |= c->gen_init_bases[k] == QUNQ(bases[i]);
		if (named || !cxx_gen_needs_construction(c, bases[i], 0))
			continue;
		cxx_ctor_use(c, bases[i], NULL, NULL, 0, false);
		c->gen_base_type = QUNQ(bases[i]);
		if (!cxx_gen_construct_at(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], NULL, 0, first, loc)) {
			c->gen_base_type = 0;
			return false;
		}
		c->gen_base_type = 0;
	}
	return cxx_gen_constructor_member_defaults(c, cxx_tag_scope_get(c, QUNQ(own)), &a->tags[AST_TYPE(a, own)->aux], first, loc, 0);
}

/*
 * A destructor with no body of its own gets one: one defaulted in its class (~T() = default), or the
 * implicit one of a class that declares none though its members and bases need destruction (made here: an
 * entity in the class like a declared one).  Its body is empty; cxx_gen_function_end adds the members'
 * and bases' destruction.  NULL: not built.
 */
static cxx_entity *cxx_gen_destructor_body(cxx *c, qty t, cxx_entity *d)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t)), *fs;
	uint32_t body;

	if (!cls || !cls->owner || cls->dependent || !a->o->emit || c->tentative || c->unevaluated)
		return NULL;
	if (!d) {
		d = cxx_declare(c, cls, cxx_dtor_id(), CXX_ENTITY_VALUE);
		if (!d)
			return NULL;
		d->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
		d->member_of = cls;
		d->fn_defaulted = 1, d->fn_implicit = 1, d->fn_inline = 1;
		cxx_vtable_declare(c, cls, d, false);	/* (it overrides a base's virtual destructor) */
	}
	if (d->gen_tried || d->fn_deleted)
		return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : d->gen_tried == 2 ? d : NULL;
	d->gen_tried = 2;	/* (being built: a member of its own type further in asks again) */
	if (!d->fn_ooc_default)
		d->fn_inline = 1;	/* (X::~X() = default; outside the class: not inline, the TU's own definition) */
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, cls, NULL);
	if (!cxx_gen_function_begin(c, d, fs, NULL, 0)) {
		d->gen_tried = 1;
		return NULL;
	}
	body = cc_statement_new(a, N_BLOCK, 0, 0, 0, 0);
	cxx_gen_function_end(c, d, body);
	d->gen_tried = 1;
	return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : NULL;
}

/*
 * The implicit default constructor of a class whose members have default initializers (or its T() = default,
 * d), made as a function the way clang makes it: an empty body, to which cxx_gen_function_end adds the
 * bases, the members and their default initializers.  NULL: not built.
 */
static cxx_entity *cxx_gen_constructor_body(cxx *c, qty t, cxx_entity *d)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t)), *fs;
	uint32_t body;

	if (!cls || !cls->owner || cls->dependent || !a->o->emit || c->tentative || c->unevaluated || c->gen_stack_depth > 40)
		return NULL;
	if (!d) {
		d = cxx_declare(c, cls, cxx_ctor_id(), CXX_ENTITY_VALUE);
		if (!d)
			return NULL;
		d->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
		d->member_of = cls;
		d->fn_defaulted = 1, d->fn_implicit = 1, d->fn_inline = 1;
	}
	if (d->gen_tried || d->fn_deleted)
		return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : d->gen_tried == 2 ? d : NULL;
	d->gen_tried = 2;
	d->fn_inline = 1;
	fs = cxx_scope_new(c, CXX_SCOPE_BLOCK, cls, NULL);
	if (!cxx_gen_function_begin(c, d, fs, NULL, 0)) {
		d->gen_tried = 1;
		return NULL;
	}
	body = cc_statement_new(a, N_BLOCK, 0, 0, 0, 0);
	cxx_gen_function_end(c, d, body);
	d->gen_tried = 1;
	return d->sym && (a->syms[d->sym].flags & SF_DEFINED) ? d : NULL;
}

/* T() of a class with default member initializers: its (made) default constructor called on target */
static bool cxx_gen_construct_by_body(cxx *c, uint32_t target, qty t, cxx_entity *d, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;

	d = cxx_gen_constructor_body(c, t, d);
	if (!d || !target)
		return false;
	cxx_gen_call(c, d, target, false, NULL, 0, loc);
	if (!c->enode)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
	c->enode = 0;
	return true;
}

/*
 * The destructor that destroying an object of the class runs: the one it declares; with none declared,
 * the one of its only base that needs destruction, when that base is at offset 0 and no member needs
 * destruction (the implicit destructor does just that).  NULL: none, or an implicit one not built yet.
 */
static cxx_entity *cxx_gen_effective_destructor(cxx *c, qty t, int depth)
{
	cc *a = c->ast;
	cxx_entity *d = cxx_gen_declared_destructor(c, t);
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16], only = CXX_TY_UNKNOWN;
	uint32_t nb;
	uint64_t off;
	bool members = false;

	if (d && d->fn_defaulted && !d->fn_deleted && !(d->sym && (a->syms[d->sym].flags & SF_DEFINED)))
		return cxx_gen_destructor_body(c, t, d);	/* ~T() = default: its body is made where it is first used */
	if (d || !cls || depth > 8)
		return d;
	if (!cxx_gen_needs_destruction(c, t, 0))
		return NULL;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft == CXX_TY_UNKNOWN)
			return NULL;
		members |= cc_type_is_record(a, ft) && cxx_gen_needs_destruction(c, ft, 0);
	}
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb && !members; i++)
		if (cxx_gen_needs_destruction(c, bases[i], 0)) {
			if (only != CXX_TY_UNKNOWN)
				members = true;
			only = bases[i];
		}
	if (!members && only != CXX_TY_UNKNOWN && cxx_class_base_offset(c, t, only, &off, 0) && !off && !cxx_class_polymorphic(c, t))
		return cxx_gen_effective_destructor(c, only, depth + 1);	/* (the base's does all there is to do) */
	return cxx_gen_destructor_body(c, t, NULL);	/* the implicit destructor, made */
}

/* ~T(): the destructor's call on the lvalue x (the class declares it); the statement goes to `out` */
static bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *d = cxx_gen_effective_destructor(c, t, 0);

	if (!x)
		return false;
	if (!d)
		return !cxx_gen_needs_destruction(c, t, 0);	/* (an implicit destructor that does something: not built yet) */
	if (d->fn_deleted)
		return false;
	cxx_gen_call(c, d, x, false, NULL, 0, loc);
	if (!c->enode)
		return false;
	cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
	c->enode = 0;
	return true;
}

/* a destructor's body is done: then its members of class type, last first, and its bases (into `after`) */
static bool cxx_gen_destructor_rest(cxx *c, cc_id_list *after, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self = cxx_gen_this_object(c, loc);
	qty own, bases[16];
	uint32_t nb;
	const tag *r;

	if (!self)
		return false;
	own = AST_NODE_AT(a, self)->ty;
	r = &a->tags[AST_TYPE(a, own)->aux];
	for (uint32_t i = r->nfield; i-- > 0;) {
		qty ft = r->f[i].ty, et = ft;

		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		if (!cc_type_is_record(a, et) || !cxx_gen_needs_destruction(c, et, 0))
			continue;
		if (!r->f[i].name || et != ft)
			return false;	/* (arrays and anonymous members of classes with destructors: not built yet) */
		if (!cxx_gen_destroy(c, member(a, cxx_gen_this_object(c, loc), r->f[i].name, loc), QUNQ(ft), after, loc))
			return false;
	}
	nb = cxx_class_bases(c, own, bases, 16);
	for (uint32_t i = nb; i-- > 0;) {
		if (!cxx_gen_needs_destruction(c, bases[i], 0))
			continue;
		c->gen_base_type = QUNQ(bases[i]);
		if (!cxx_gen_destroy(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], after, loc)) {
			c->gen_base_type = 0;
			return false;
		}
		c->gen_base_type = 0;
	}
	return true;
}

/*
 * A local object of class type: its declaration, its construction from args (cxx_ctor_use was called for
 * it), and its destructor at the end of its scope (the emitter's scope cleanup, as C's
 * __attribute__((cleanup))).
 */
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *dtor;
	cc_id_list *block = c->gen_block;
	/* (what cxx_ctor_use chose: naming the destructor below resolves nothing, but keep it apart) */
	cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
	bool found = c->ctor_found, none = c->ctor_none;
	qty il_types[16];
	uint8_t il_lval[16];
	uint32_t il_n = c->ctor_ilist_n, il_brace[16];

	memcpy(il_types, c->ctor_ilist_types, sizeof il_types), memcpy(il_lval, c->ctor_ilist_lval, sizeof il_lval);
	memcpy(il_brace, c->ctor_ilist_brace, sizeof il_brace);

	if (!cxx_gen_on(c) || !s)
		return;
	if (!block || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	dtor = cxx_gen_effective_destructor(c, t, 0);
	if (dtor) {
		attrs at;

		if (dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1 || (cxx_member_body_use(c, dtor), 0)) {
			cxx_gen_fail(c);
			return;
		}
		memset(&at, 0, sizeof at);
		at.cleanup = cxx_gen_dtor_sym(dtor);
		cc_attribute_apply_symbol(a, &a->syms[s], &at);
	} else if (cxx_gen_needs_destruction(c, t, 0)) {
		cxx_gen_fail(c);	/* (an implicit destructor that destroys members: not built yet) */
		return;
	}
	cc_id_list_append(a, block, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, 0));
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none, c->ctor_ilist = il, c->ctor_ilist_n = il_n;
	memcpy(c->ctor_ilist_types, il_types, sizeof il_types), memcpy(c->ctor_ilist_lval, il_lval, sizeof il_lval);
	memcpy(c->ctor_ilist_brace, il_brace, sizeof il_brace);
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, s, loc), t, args, nargs, block, loc))
		cxx_gen_fail(c);
}

/* ---- new and delete ------------------------------------------------------------------------- */

/* a call of one of the builtin table's library functions (0: not built) */
static uint32_t cxx_gen_library_call(cxx *c, const char *name, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	uint32_t saved = c->enode, n;

	c->enode = 0;
	cxx_gen_builtin(c, name, strlen(name), args, nargs, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}

/*
 * new T(args), new T{args}, new T, new T[n], new (p) T(args): the storage (operator new, or the placement
 * argument p), the object constructed in it (cxx_ctor_use was called for a class with the arguments), the
 * pointer the result.  ninit: -1 no initializer, else the arguments' count.  Arrays: of scalars only.
 */
VPRIV void cxx_gen_new(cxx *c, qty t, const uint32_t *place, uint32_t nplace, uint32_t count, bool array, const uint32_t *init,
		       int ninit, uint32_t loc)
{
	cc *a = c->ast;
	qty pt;
	uint32_t mem, tmp, obj, size, last;
	cc_id_list l;

	c->enode = 0;
	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, t) == TY_VOID || AST_TYPE_KIND(a, t) == TY_FUNC ||
	    !(cxx_gen_object_type_ok(c, t) || cxx_gen_type_ok(c, t)) || cxx_gen_is_reference(c, t))
		return;
	if (array && (cc_type_is_record(a, t) || ninit > 0 || !count))
		return;	/* (an array of a class, an array from a list: not built yet) */
	pt = cc_type_pointer(a, t);
	size = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	if (nplace == 1 && place[0] && AST_TYPE_KIND(a, AST_NODE_AT(a, cc_conversion_decay(a, place[0]))->ty) == TY_PTR)
		mem = cc_conversion_decay(a, place[0]);	/* new (p) T: operator new(size_t, void *) gives p back */
	else if (nplace)
		return;	/* (nothrow, alignment, user placement forms: not built yet) */
	else if (array) {
		uint32_t n = cc_conversion_apply(a, cc_conversion_decay(a, count), cc_type_basic(TY_ULONG));

		size = cc_binary_build(a, LEX_TOKEN_STAR, n, size, loc);
		mem = cxx_gen_library_call(c, "__vcc_new_array", &size, 1, loc);
	} else
		mem = cxx_gen_library_call(c, "__builtin_operator_new", &size, 1, loc);
	if (!mem || !(tmp = cxx_gen_temporary(c, pt, loc)))
		return;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc),
					cc_cast_insert(a, mem, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, mem)->ty, pt, mem)), loc), 0, 0));
	obj = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc);
	if (array) {
		if (ninit == 0) {	/* new T[n](): zeroed */
			uint32_t z[3] = { cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP),
					  cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), size };

			if (!(last = cxx_gen_library_call(c, "__builtin_memset", z, 3, loc)))
				return;
			cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, last, 0, 0));
		}
	} else if (cc_type_is_record(a, t) && AST_TYPE_KIND(a, t) != TY_ARRAY) {
		/* (no initializer: default-initialized, as T() is for a class with a constructor) */
		if (!cxx_gen_construct_at(c, obj, QUNQ(t), init, ninit < 0 ? 0 : (uint32_t)ninit, &l, loc))
			return;
	} else if (ninit == 1 || ninit == 0) {
		uint32_t v = ninit ? cxx_gen_bind(c, init[0], QUNQ(t), loc) : cc_conversion_apply(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), QUNQ(t));

		if (!v)
			return;
		if (t != QUNQ(t)) {
			qty upt = cc_type_pointer(a, QUNQ(t));

			obj = cc_dereference_build(a, cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), upt, CK_NOP), loc);
		}
		cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, obj, v, loc), 0, 0));
	} else if (ninit > 1)
		return;
	c->enode = cxx_gen_chain(c, &l, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc);
}

/* delete p, delete[] p: the object's destructor (a class's), then operator delete (sized, as clang does) */
VPRIV void cxx_gen_delete(cxx *c, uint32_t p, bool array, uint32_t loc)
{
	cc *a = c->ast;
	qty pt, t;
	uint32_t tmp, n, args[2];
	cc_id_list l;
	cxx_entity *d;

	c->enode = 0;
	if (!cxx_gen_on(c) || !p)
		return;
	p = cc_node_rvalue(a, p);
	pt = QUNQ(AST_NODE_AT(a, p)->ty);
	if (AST_TYPE_KIND(a, pt) != TY_PTR || AST_TYPE(a, pt)->ext)
		return;
	t = AST_TYPE(a, pt)->base;
	if (AST_TYPE_KIND(a, t) == TY_VOID || (array && cc_type_is_record(a, t) && cxx_gen_needs_destruction(c, t, 0)))
		return;	/* (an array of objects with destructors: its count cookie, not built yet) */
	if (cc_type_is_record(a, t) && cxx_class_polymorphic(c, t)) {
		/* a polymorphic object: its deleting destructor (D0) through the vtable, the slot after the complete
		   one's -- tmp = p, tmp ? (*(F ***)tmp)[slot](tmp) : (void)0 */
		cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
		qty pf, ppf, pppf;
		uint32_t vptr, fn, call, cond;

		d = array ? NULL : cxx_gen_effective_destructor(c, t, 0);
		if (!d || !d->fn_virtual || !d->vslot || !cls || !cxx_vtable_usable(c, cls) || !cxx_gen_name(c, d) || !d->sym0)
			return;
		if (!(tmp = cxx_gen_temporary(c, pt, loc)))
			return;
		pf = cc_type_pointer(a, a->syms[d->sym0].ty), ppf = cc_type_pointer(a, pf), pppf = cc_type_pointer(a, ppf);
		vptr = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), pppf, CK_NOP), loc));
		fn = cc_node_rvalue(a, cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, vptr,
				cc_node_integer_new(a, d->vslot, cc_type_basic(TY_LONG), loc), loc), loc));
		call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
		{
			cc_id_list al;

			cc_id_list_init(&al);
			cc_id_list_append(a, &al, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)));
			AST_NODE_AT(a, call)->a = fn;
			AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &al);
			AST_NODE_AT(a, call)->c = al.n;
		}
		cond = cc_conditional_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), call,
					    cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID), loc);
		c->enode = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc,
						cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), p, loc), cond);
		return;
	}
	if (!(tmp = cxx_gen_temporary(c, pt, loc)))
		return;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), p, loc), 0, 0));
	d = cc_type_is_record(a, t) ? cxx_gen_effective_destructor(c, t, 0) : NULL;
	if (cc_type_is_record(a, t) && !d && cxx_gen_needs_destruction(c, t, 0))
		return;
	args[0] = cc_cast_insert(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP);
	args[1] = cc_node_integer_new(a, (i128)AST_TYPE(a, QUNQ(t))->size, cc_type_basic(TY_ULONG), loc);
	n = array ? cxx_gen_library_call(c, "__vcc_delete_array", args, 1, loc) : cxx_gen_library_call(c, "__builtin_operator_delete", args, 2, loc);
	if (!n)
		return;
	if (d) {
		/* (the destructor runs on the object; both only when p is not null) */
		uint32_t saved = c->enode;

		c->enode = 0;
		cxx_gen_call(c, d, cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc)), loc), false, NULL, 0, loc);
		if (!c->enode) {
			c->enode = saved;
			return;
		}
		n = cc_node_new_operands(a, N_COMMA, AST_NODE_AT(a, n)->ty, loc, c->enode, n);
		c->enode = saved;
	}
	{
		/* p ? (dtor, delete) : (void)0 */
		uint32_t cond = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
		uint32_t none = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID);

		n = cc_conditional_build(a, cond, cc_cast_insert(a, n, cc_type_basic(TY_VOID), CK_VOID), none, loc);
	}
	c->enode = cxx_gen_chain(c, &l, n, loc);
}

/* x.~T(), p->~T(): the destructor's call (a class with none to run, a scalar: nothing), as an expression */
VPRIV void cxx_gen_destroy_object(cxx *c, uint32_t x, bool arrow, uint32_t loc)
{
	cc *a = c->ast;
	qty t;
	cxx_entity *d;

	c->enode = 0;
	if (!cxx_gen_on(c) || !x)
		return;
	if (arrow) {
		x = cc_node_rvalue(a, x);
		if (AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) != TY_PTR)
			return;
		x = cc_dereference_build(a, x, loc);
	}
	t = QUNQ(AST_NODE_AT(a, x)->ty);
	if (!cc_type_is_record(a, t) || !cxx_gen_needs_destruction(c, t, 0)) {
		c->enode = cc_cast_insert(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), cc_type_basic(TY_VOID), CK_VOID);
		return;
	}
	if (!(d = cxx_gen_effective_destructor(c, t, 0)) || cxx_class_polymorphic(c, t))
		return;	/* (a virtual destructor: through the vtable, not built yet) */
	cxx_gen_call(c, d, x, false, NULL, 0, loc);
}

/* ---- virtual functions ----------------------------------------------------------------------- */

static bool cxx_vt_is_dtor(const cxx_entity *e)
{
	return cxx_gen_is_special(e) && vintern_ents[e->name].s[1] != 'c';
}

/* does y override x: the same name, parameter types and constness (a destructor overrides a destructor) */
static bool cxx_vt_same(cxx *c, const cxx_entity *x, const cxx_entity *y)
{
	cc *a = c->ast;
	const ctype *fx, *fy;

	if (cxx_vt_is_dtor(x) || cxx_vt_is_dtor(y))
		return cxx_vt_is_dtor(x) && cxx_vt_is_dtor(y);
	if (x->name != y->name || x->ty == CXX_TY_UNKNOWN || y->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, x->ty) != TY_FUNC ||
	    AST_TYPE_KIND(a, y->ty) != TY_FUNC || x->fn_const != y->fn_const)
		return false;
	fx = AST_TYPE(a, x->ty), fy = AST_TYPE(a, y->ty);
	if (fx->n != fy->n)
		return false;
	for (uint32_t i = 0; i < fx->n; i++)
		if (QUNQ(a->params[fx->aux + i]) != QUNQ(a->params[fy->aux + i]))
			return false;
	return true;
}

static void cxx_vt_push(cxx_scope *cls, cxx_entity *e)
{
	if (cls->nvfuncs == cls->cvfuncs)
		cls->vfuncs = realloc(cls->vfuncs, (cls->cvfuncs = cls->cvfuncs ? cls->cvfuncs * 2 : 8) * sizeof *cls->vfuncs);
	cls->vfuncs[cls->nvfuncs++] = e;
}

/* the class's vtable starts as its primary base's (the one base with a vtable) */
static void cxx_vtable_init(cxx *c, cxx_scope *cls)
{
	if (cls->vt_init)
		return;
	cls->vt_init = 1;
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *b = cls->used[i];

		if (!b || b->kind != CXX_SCOPE_CLASS)
			continue;
		cxx_vtable_init(c, b);
		if (b->vt_bad)
			cls->vt_bad = 1;
		if (!b->nvfuncs)
			continue;
		if (cls->nvfuncs) {
			cls->vt_bad = 1;	/* (a second vtable, with thunks: not built yet) */
			continue;
		}
		for (uint32_t k = 0; k < b->nvfuncs; k++)
			cxx_vt_push(cls, b->vfuncs[k]);
	}
}

/* a non-static member function is declared in a class: its vtable slot, if it is virtual or overrides one */
VPRIV void cxx_vtable_declare(cxx *c, cxx_scope *cls, cxx_entity *e, bool virt_kw)
{
	bool found = false;

	if (!cls || cls->kind != CXX_SCOPE_CLASS || c->tentative || e->fn_virtual || (cxx_gen_is_special(e) && !cxx_vt_is_dtor(e)))
		return;
	cxx_vtable_init(c, cls);
	for (uint32_t i = 0; i < cls->nvfuncs; i++)
		if (cls->vfuncs[i] != e && cxx_vt_same(c, cls->vfuncs[i], e)) {
			if (!found)
				e->vslot = i + 1;
			cls->vfuncs[i] = e;
			found = true;
		}
	if (!found && !virt_kw)
		return;
	if (!found) {
		e->vslot = cls->nvfuncs + 1;
		cxx_vt_push(cls, e);
		if (cxx_vt_is_dtor(e))
			cxx_vt_push(cls, e);
	}
	e->fn_virtual = 1;
	{
		/* = 0 after the declarator (and override, final) */
		uint32_t k = 0;

		while (cxx_peek(c, k)->kind == LEX_TOKEN_IDENTIFIER && !cxx_peek(c, k)->kw && k < 4)
			k++;
		if (cxx_peek(c, k)->kind == LEX_TOKEN_EQUAL && cxx_peek(c, k + 1)->len == 1 && cxx_peek(c, k + 1)->s[0] == '0')
			e->fn_pure = 1;
	}
	if (cls->nvown == cls->cvown)
		cls->vown = realloc(cls->vown, (cls->cvown = cls->cvown ? cls->cvown * 2 : 8) * sizeof *cls->vown);
	cls->vown[cls->nvown++] = e;
}

/* can the class's vtable be built here? */
static bool cxx_vtable_usable(cxx *c, cxx_scope *cls)
{
	if (!cls || cls->kind != CXX_SCOPE_CLASS || !cls->owner || cls->owner->ty == CXX_TY_UNKNOWN || cls->dependent)
		return false;
	cxx_vtable_init(c, cls);
	if (cls->vt_bad || !cls->nvfuncs)
		return false;
	for (uint32_t i = 0; i < cls->nvfuncs; i++) {
		const cxx_entity *e = cls->vfuncs[i];

		if (!cxx_vt_is_dtor(e) || e->member_of == cls)
			continue;
		/* a base's virtual destructor and none declared here: the implicit one is the base's if this class's
		   own members need no destruction (else: an implicit destructor, not built yet) */
		for (uint32_t k = 0; k < cls->nfield_types; k++) {
			qty ft = cls->field_types[k];

			while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == TY_ARRAY)
				ft = AST_TYPE(c->ast, ft)->base;
			if (ft == CXX_TY_UNKNOWN)
				return false;
			if (cc_type_is_record(c->ast, ft) && cxx_gen_needs_destruction(c, ft, 0)) {
				/* (members to destroy: the class's own implicit destructor, made now; it overrides the
				   base's in the vtable) */
				cxx_entity *d;

				if (c->tentative || c->unevaluated || c->vt_making > 2)
					return false;
				c->vt_making++;
				d = cxx_gen_effective_destructor(c, cls->owner->ty, 0);
				c->vt_making--;
				if (!d || d->member_of != cls || !d->fn_virtual)
					return false;
				return cxx_vtable_usable(c, cls);
			}
		}
	}
	return true;
}

/* the vtable's symbol (_ZTV...), declared; the TU's end defines it where it belongs here (0: not built) */
static uint32_t cxx_vtable_symbol(cxx *c, cxx_scope *cls)
{
	cc *a = c->ast;
	char name[2048];
	attrs at;
	uint32_t s;

	if (cls->vt_sym)
		return cls->vt_sym;
	if (!cxx_vtable_usable(c, cls) || !cxx_mangle_class_data(c, "TV", cls->owner->ty, name, sizeof name))
		return 0;
	s = cc_symbol_new(a, S_VAR, cls->owner->name, cc_type_array(a, cc_type_pointer(a, cc_type_basic(TY_VOID)), cls->nvfuncs + 2, 0), 0);
	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	a->syms[s].sc = SC_EXTERN;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[s], &at);
	cls->vt_sym = s;
	if (c->nvt_list == c->cvt_list)
		c->vt_list = realloc(c->vt_list, (c->cvt_list = c->cvt_list ? c->cvt_list * 2 : 32) * sizeof *c->vt_list);
	c->vt_list[c->nvt_list++] = cls;
	/* a class template's instance: its virtual functions' bodies are wanted now (the vtable names them) */
	for (uint32_t i = 0; i < cls->nvfuncs; i++)
		if (cls->vfuncs[i]->late && !cls->vfuncs[i]->late_done)
			cxx_member_body_use(c, cls->vfuncs[i]);
	return s;
}

/* this's vptr = the class's vtable (past its two header words): the statement (0: not built) */
static uint32_t cxx_gen_vptr_store_at(cxx *c, cxx_scope *cls, uint32_t self, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), vppp = cc_type_pointer(a, cc_type_pointer(a, vp));
	uint32_t vs = cxx_vtable_symbol(c, cls), addr, target;

	if (!vs || !self)
		return 0;
	addr = cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, vs, loc)),
			       cc_node_integer_new(a, 2, cc_type_basic(TY_LONG), loc), loc);
	target = cc_dereference_build(a, cc_cast_insert(a, self, vppp, cc_conversion_cast_kind(a, AST_NODE_AT(a, self)->ty, vppp, self)), loc);
	return cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, addr, loc), 0, 0);
}

static uint32_t cxx_gen_function_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t fn = cc_node_new(c->ast, N_VAR, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, fn)->a = s;
	AST_NODE_AT(c->ast, fn)->flags = NF_LVALUE;
	return cc_node_rvalue(c->ast, fn);
}

static uint32_t cxx_gen_vptr_store(cxx *c, cxx_scope *cls, uint32_t loc)
{
	uint32_t self;

	cxx_gen_this(c, loc);
	self = c->enode;
	c->enode = 0;
	return cxx_gen_vptr_store_at(c, cls, self, loc);
}

/*
 * An object of a class with no constructor declared is default-initialized where it stands (target: an
 * lvalue naming it, without side effects): its bases and members of class type, then its vptr.  False: not
 * built (members with default initializers, arrays of classes with constructors).
 */
static bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc);

static bool cxx_gen_default_construct(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth)
{
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));

	if (!target)
		return false;
	if (!cxx_gen_needs_construction(c, t, 0))
		return true;
	if (!cls || depth > 8)
		return false;
	cxx_ctor_use(c, QUNQ(t), NULL, NULL, 0, false);
	if (c->ctor_found && c->ctor_picked && c->ctor_picked->member_of == cls)
		return cxx_gen_construct_at(c, target, QUNQ(t), NULL, 0, out, loc);
	for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
		if (e->member_of == cls)
			return false;	/* (constructors, none of them for no arguments) */
	if (cxx_class_has_member_initializer(c, t))
		return cxx_gen_construct_by_body(c, target, t, NULL, out, loc);
	return cxx_gen_default_construct_parts(c, target, t, out, loc, depth);
}

/* ... what the implicit default constructor does: the bases, the members of class type, the vptr */
static bool cxx_gen_default_construct_parts(cxx *c, uint32_t target, qty t, cc_id_list *out, uint32_t loc, int depth)
{
	cc *a = c->ast;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	qty bases[16];
	uint32_t nb;
	const tag *r;

	if (!target || !cls || depth > 8)
		return false;
	if (AST_TYPE_KIND(a, QUNQ(t)) == TY_UNION)
		return true;	/* (a union: no member is made active) */
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++)
		if (!cxx_gen_default_construct(c, cxx_gen_base_object(c, target, bases[i], loc), bases[i], out, loc, depth + 1))
			return false;
	r = &a->tags[AST_TYPE(a, QUNQ(t))->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty, et = ft;

		while (AST_TYPE_KIND(a, et) == TY_ARRAY)
			et = AST_TYPE(a, et)->base;
		if (!cc_type_is_record(a, et) || !cxx_gen_needs_construction(c, et, 0))
			continue;
		if (et != ft || !r->f[i].name)
			return false;
		if (!cxx_gen_default_construct(c, member(a, target, r->f[i].name, loc), ft, out, loc, depth + 1))
			return false;
	}
	if (cxx_class_polymorphic(c, t)) {
		uint32_t st = cxx_gen_vptr_store_at(c, cls, cc_address_of_build(a, target, loc), loc);

		if (!st)
			return false;
		cc_id_list_append(a, out, st);
	}
	return true;
}

/* a library function by its symbol name, void f(void *) or void f(void) */
static uint32_t cxx_gen_runtime_symbol(cxx *c, const char *name, bool pointer_param)
{
	cc *a = c->ast;
	qty ps[1] = { cc_type_pointer(a, cc_type_basic(TY_VOID)) };
	uint32_t s = cc_symbol_new(a, S_FUNC, vintern_cstr(name), cxx_type_function(c, cc_type_basic(TY_VOID), ps, pointer_param ? 1 : 0, false), 0);

	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	return s;
}

/* a virtual destructor is defined: the deleting destructor D0 = { D1(this); operator delete(this); } */
static void cxx_gen_deleting_destructor(cxx *c, cxx_entity *f)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t loc = a->syms[f->sym].loc, ps, self, call, body, saved_fn = a->fn;
	cc_id_list psyms, stmts, args;

	if (!f->sym0 || !f->sym1 || (a->syms[f->sym0].flags & SF_DEFINED))
		return;
	if (!c->gen_delete_sym)
		c->gen_delete_sym = cxx_gen_runtime_symbol(c, "_ZdlPv", true);
	cc_scope_open(a);
	a->fn = f->sym0;
	ps = cc_symbol_new(a, S_VAR, vintern_cstr("this"), cxx_gen_this_type(c, f), loc);
	a->syms[ps].flags |= SF_PARAM;
	a->syms[ps].sc = SC_AUTO;
	cc_id_list_init(&psyms);
	cc_id_list_append(a, &psyms, ps);
	cc_id_list_init(&stmts);
	cc_id_list_init(&args);
	cc_id_list_append(a, &args, cc_node_rvalue(a, cxx_gen_var_node(c, ps, loc)));
	call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
	AST_NODE_AT(a, call)->a = cxx_gen_function_node(c, f->sym1, loc);
	AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
	AST_NODE_AT(a, call)->c = 1;
	cc_id_list_append(a, &stmts, cc_statement_new(a, N_EXPR, loc, call, 0, 0));
	cc_id_list_init(&args);
	self = cc_node_rvalue(a, cxx_gen_var_node(c, ps, loc));
	cc_id_list_append(a, &args, cc_cast_insert(a, self, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, self)->ty, vp, self)));
	call = cc_node_new(a, N_CALL, cc_type_basic(TY_VOID), loc);
	AST_NODE_AT(a, call)->a = cxx_gen_function_node(c, c->gen_delete_sym, loc);
	AST_NODE_AT(a, call)->b = cc_id_list_freeze(a, &args);
	AST_NODE_AT(a, call)->c = 1;
	cc_id_list_append(a, &stmts, cc_statement_new(a, N_EXPR, loc, call, 0, 0));
	body = cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &stmts), stmts.n);
	cc_scope_close(a);
	a->fn = saved_fn;
	a->syms[f->sym0].body = body;
	a->syms[f->sym0].flags |= SF_DEFINED;
	a->syms[f->sym0].defloc = loc;
	AST_NODE_AT(a, body)->a = cc_id_list_freeze(a, &psyms);
	AST_NODE_AT(a, body)->aux = 1;
	a->toplevel[a->ntoplevel++] = f->sym0;
}

/*
 * The TU's end: the vtables it defines.  A class with a key function (its first virtual function that is
 * neither pure nor inline) has its vtable where that function is defined; any other class (a class
 * template's instance too) in every TU that names it, weak.  The type info slot is null for now (RTTI:
 * not built yet).
 */
/*
 * At the end of the TU: a complete-object constructor or destructor (C1, D1) referred to and not defined,
 * whose base-object one (C2, D2: the same code without virtual bases) is defined here, is an alias of it
 * (two declarations of one inline member, one of them naming the C1 apart).
 */
VPRIV void cxx_gen_complete_object_aliases(cxx *c)
{
	cc *a = c->ast;
	struct { const char *name; uint32_t sym; } *t = (void *)c->sym_names;

	for (uint32_t k = 0; k < c->csym_names; k++) {
		const char *nm = t[k].name;
		uint32_t s = t[k].sym;
		size_t l;

		if (!nm || !s || (a->syms[s].flags & SF_DEFINED) || a->syms[s].kind != S_FUNC || (l = strlen(nm)) >= 2048)
			continue;
		for (size_t i = 3; i + 1 < l; i++) {
			char two[2048];
			uint32_t d;

			if ((nm[i] != 'C' && nm[i] != 'D') || nm[i + 1] != '1')
				continue;
			memcpy(two, nm, l + 1);
			two[i + 1] = '2';
			d = cxx_gen_symbol_named(c, two, 0);
			if (d && d != s && (a->syms[d].flags & SF_DEFINED) && a->syms[d].attrs) {
				attrs at;

				memset(&at, 0, sizeof at);
				at.alias = a->attr[a->syms[d].attrs].asmname;
				at.weak = 1;
				cc_attribute_apply_symbol(a, &a->syms[s], &at);
				a->syms[s].flags |= SF_DEFINED;
				a->toplevel[a->ntoplevel++] = s;
				break;
			}
		}
	}
}

VPRIV void cxx_gen_vtables_emit(cxx *c)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	for (uint32_t i = 0; i < c->nvt_list; i++) {
		cxx_scope *cls = c->vt_list[i];
		sym *y = &a->syms[cls->vt_sym];
		const cxx_entity *key = NULL;
		cc_id_list v;
		uint32_t n, seen_dtor = 0;
		bool ok = true;

		for (uint32_t k = 0; k < cls->nvown && !key && !(cls->owner && cls->owner->instance); k++)
			if (!cls->vown[k]->fn_pure && !cls->vown[k]->fn_inline && !cls->vown[k]->fn_defaulted && !cls->vown[k]->fn_deleted)
				key = cls->vown[k];
		if (key && !(key->sym && (a->syms[key->sym].flags & SF_DEFINED)))
			continue;	/* (the TU that defines the key function has it) */
		cc_id_list_init(&v);
		for (uint32_t k = 0; k < cls->nvfuncs + 2 && ok; k++) {
			cxx_entity *e = k >= 2 ? cls->vfuncs[k - 2] : NULL;
			uint32_t val, s = 0;

			if (k == 1 && cls->owner && (s = cxx_gen_typeinfo(c, cls->owner->ty)) != 0)
				val = cxx_gen_data_address(c, s, 0);	/* the class's type info */
			else if (!e)
				val = cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), 0);
			else {
				if (e->fn_pure) {
					if (!c->gen_pure_sym)
						c->gen_pure_sym = cxx_gen_runtime_symbol(c, "__cxa_pure_virtual", false);
					s = c->gen_pure_sym;
				} else if (e->fn_deleted || !cxx_gen_name(c, e))
					ok = false;
				else if (cxx_vt_is_dtor(e))
					s = seen_dtor++ ? e->sym0 : e->sym1;
				else
					s = e->sym;
				if (!s) {
					ok = false;
					break;
				}
				val = cxx_gen_function_node(c, s, 0);
			}
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, cc_cast_insert(a, val, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, val)->ty, vp, val)));
		}
		if (!ok)
			continue;
		n = cc_node_new(a, N_INIT, y->ty, 0);
		AST_NODE_AT(a, n)->aux = 1;
		AST_NODE_AT(a, n)->c = v.n / 3;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
		if (!cxx_gen_static_ok(a, y->ty, n))
			continue;
		y = &a->syms[cls->vt_sym];
		y->sc = 0;
		y->body = n;
		y->flags |= SF_DEFINED;
		if (!key) {
			attrs at;

			memset(&at, 0, sizeof at);
			at.weak = 1;
			cc_attribute_apply_symbol(a, y, &at);
			y->flags |= SF_LINKONCE;	/* (emitted only if the TU's code refers to it) */
		}
		a->toplevel[a->ntoplevel++] = cls->vt_sym;
	}
	cxx_gen_typeinfos_emit(c);
}

/* ---- run-time type information and exceptions ------------------------------------------------ */

/* a data symbol by its assembler name, of type void *[n] (n 0: its size is not this TU's business) */
static uint32_t cxx_gen_data_symbol(cxx *c, const char *name, uint32_t n)
{
	cc *a = c->ast;
	attrs at;
	uint32_t s = cc_symbol_new(a, S_VAR, vintern_cstr("__ti"), cc_type_array(a, cc_type_pointer(a, cc_type_basic(TY_VOID)), n ? n : 1, 0), 0);

	a->syms[s].flags |= SF_GLOBAL;
	a->syms[s].depth = 0;
	a->syms[s].sc = SC_EXTERN;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	cc_attribute_apply_symbol(a, &a->syms[s], &at);
	return s;
}

/*
 * The type info object of type t (_ZTI...): a class's is declared here and defined at the TU's end where
 * it belongs to this TU; a fundamental type's (and a pointer to one's) is the C++ runtime's.  0: not built
 * (pointers to classes, functions, members).
 */
VPRIV uint32_t cxx_gen_typeinfo(cxx *c, qty t)
{
	cc *a = c->ast;
	char name[2048] = "_ZTI";
	cxx_scope *cls;
	int k;
	bool pod, empty;

	if (t == CXX_TY_UNKNOWN)
		return 0;
	t = QUNQ(t);
	k = AST_TYPE_KIND(a, t);
	if (k == TY_STRUCT || k == TY_UNION) {
		cls = cxx_tag_scope_get(c, t);
		if (!cls || !cxx_class_layout_info(c, t, &pod, &empty))
			return 0;
		if (cls->ti_sym)
			return cls->ti_sym;
		if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
			return 0;
		cls->ti_sym = cxx_gen_data_symbol(c, name, 0);
		if (c->nti_list == c->cti_list)
			c->ti_list = realloc(c->ti_list, (c->cti_list = c->cti_list ? c->cti_list * 2 : 32) * sizeof *c->ti_list);
		c->ti_list[c->nti_list++] = cls;
		return cls->ti_sym;
	}
	if (k == TY_PTR && !AST_TYPE(a, t)->ext) {
		/* the runtime has void *, char * and the like (and their const forms) */
		qty b = AST_TYPE(a, t)->base;
		int bk = b == CXX_TY_UNKNOWN ? -1 : (int)AST_TYPE_KIND(a, b);

		if (!(bk == TY_VOID || (bk >= TY_BOOL && bk <= TY_ULLONG) || bk == TY_FLOAT || bk == TY_DOUBLE))
			return 0;
	} else if (!((k >= TY_BOOL && k <= TY_ULLONG) || k == TY_FLOAT || k == TY_DOUBLE))
		return 0;
	if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
		return 0;
	return cxx_gen_data_symbol(c, name, 0);
}

/* &sym[index] as a void * constant (an initializer's element) */
static uint32_t cxx_gen_data_address(cxx *c, uint32_t s, uint32_t index)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t n = cc_conversion_decay(a, cxx_gen_var_node(c, s, 0));

	if (index)
		n = cc_binary_build(a, LEX_TOKEN_PLUS, n, cc_node_integer_new(a, index, cc_type_basic(TY_LONG), 0), 0);
	return cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, n)->ty, vp, n));
}

/* the vtable's key function rule, for a class's type info too: does this TU define the class's vtable and RTTI? */
static bool cxx_gen_class_data_here(cxx *c, cxx_scope *cls)
{
	cc *a = c->ast;
	const cxx_entity *key = NULL;

	for (uint32_t k = 0; k < cls->nvown && !key && !(cls->owner && cls->owner->instance); k++)
		if (!cls->vown[k]->fn_pure && !cls->vown[k]->fn_inline && !cls->vown[k]->fn_defaulted && !cls->vown[k]->fn_deleted)
			key = cls->vown[k];
	return !key || (key->sym && (a->syms[key->sym].flags & SF_DEFINED));
}

/*
 * The TU's end: the type info objects it defines (weak; a class with a key function has its own where that
 * function is defined, as its vtable): _ZTS, the type's name, and _ZTI, an object of the runtime's
 * __class_type_info (no bases), __si_class_type_info (one base at offset 0) or __vmi_class_type_info.
 */
static void cxx_gen_typeinfos_emit(cxx *c)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t vt_class = 0, vt_si = 0, vt_vmi = 0;

	for (uint32_t i = 0; i < c->nti_list; i++) {	/* (the list grows: a base's type info) */
		cxx_scope *cls = c->ti_list[i];
		qty t = cls->owner->ty, bases[16];
		uint32_t nb = cxx_class_bases(c, t, bases, 16), bti[16], n, words = 0, ts, si, vals[40];
		char name[2048] = "_ZTS";
		uint64_t off[16];
		cc_id_list v;
		attrs at;
		sym *y;
		bool ok = true;

		if ((cls->nvfuncs || cls->nvown) && !cxx_gen_class_data_here(c, cls))
			continue;	/* (the key function's TU has it) */
		if (!cxx_mangle_type(c, t, name + 4, sizeof name - 4))
			continue;
		for (uint32_t k = 0; k < nb && ok; k++)
			ok = (bti[k] = cxx_gen_typeinfo(c, bases[k])) != 0 && cxx_class_base_offset(c, t, bases[k], &off[k], 0);
		if (!ok)
			continue;
		/* the name: a weak array of char */
		si = cxx_gen_text(c, name + 4);
		ts = cc_symbol_new(a, S_VAR, vintern_cstr("__ts"), cc_type_array(a, QT(TY_CHAR, Q_CONST), strlen(name + 4) + 1, 0), 0);
		a->syms[ts].flags |= SF_GLOBAL;
		a->syms[ts].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		at.weak = 1;
		cc_attribute_apply_symbol(a, &a->syms[ts], &at);
		a->syms[ts].flags |= SF_LINKONCE;
		n = cc_node_new(a, N_STR, a->syms[ts].ty, 0);
		AST_NODE_AT(a, n)->a = si;
		AST_NODE_AT(a, n)->flags = NF_LVALUE;
		a->syms[ts].body = n;
		a->syms[ts].flags |= SF_DEFINED;
		a->toplevel[a->ntoplevel++] = ts;
		/* the object's words */
		if (!nb) {
			if (!vt_class)
				vt_class = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv117__class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_class, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
		} else if (nb == 1 && !off[0]) {
			if (!vt_si)
				vt_si = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv120__si_class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_si, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
			vals[words++] = cxx_gen_data_address(c, bti[0], 0);
		} else {
			if (!vt_vmi)
				vt_vmi = cxx_gen_data_symbol(c, "_ZTVN10__cxxabiv121__vmi_class_type_infoE", 0);
			vals[words++] = cxx_gen_data_address(c, vt_vmi, 2);
			vals[words++] = cxx_gen_data_address(c, ts, 0);
			/* flags (none) and the number of bases, two unsigned ints; then each base and its offset << 8 | public */
			n = cc_node_integer_new(a, (i128)((uint64_t)nb << 32), cc_type_basic(TY_LONG), 0);
			vals[words++] = cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, n));
			for (uint32_t k = 0; k < nb; k++) {
				vals[words++] = cxx_gen_data_address(c, bti[k], 0);
				n = cc_node_integer_new(a, (i128)((off[k] << 8) | 2), cc_type_basic(TY_LONG), 0);
				vals[words++] = cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, cc_type_basic(TY_LONG), vp, n));
			}
		}
		cc_id_list_init(&v);
		for (uint32_t k = 0; k < words; k++) {
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, k);
			cc_id_list_append(a, &v, vals[k]);
		}
		y = &a->syms[cls->ti_sym];
		y->ty = cc_type_array(a, vp, words, 0);
		n = cc_node_new(a, N_INIT, y->ty, 0);
		AST_NODE_AT(a, n)->aux = 1;
		AST_NODE_AT(a, n)->c = words;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
		if (!cxx_gen_static_ok(a, y->ty, n))
			continue;
		y = &a->syms[cls->ti_sym];
		y->sc = 0;
		y->body = n;
		y->flags |= SF_DEFINED;
		memset(&at, 0, sizeof at);
		at.weak = 1;
		cc_attribute_apply_symbol(a, y, &at);
		y->flags |= SF_LINKONCE;
		a->toplevel[a->ntoplevel++] = cls->ti_sym;
	}
}

/* the exception runtime's functions (libc++abi), by index */
static uint32_t cxx_gen_eh_symbol(cxx *c, int which)
{
	static const struct { const char *name; char ret, np; } fns[] = {
		{ "__cxa_allocate_exception", 'p', 1 }, { "__cxa_throw", 'v', 3 }, { "__cxa_rethrow", 'v', 0 },
		{ "__cxa_begin_catch", 'p', 1 }, { "__cxa_end_catch", 'v', 1 },	/* (called as a scope's cleanup: with its address) */
	};
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };

	if (!c->gen_eh_syms[which]) {
		uint32_t s;

		if (which == 0)
			ps[0] = cc_type_basic(TY_ULONG);
		s = cc_symbol_new(a, S_FUNC, vintern_cstr(fns[which].name), cxx_type_function(c, fns[which].ret == 'p' ? vp : cc_type_basic(TY_VOID), ps, (uint32_t)fns[which].np, false), 0);
		a->syms[s].flags |= SF_GLOBAL;
		a->syms[s].depth = 0;
		c->gen_eh_syms[which] = s;
	}
	return c->gen_eh_syms[which];
}

static uint32_t cxx_gen_runtime_call(cxx *c, uint32_t fs, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cc_id_list l;
	uint32_t n;

	cc_id_list_init(&l);
	for (uint32_t i = 0; i < nargs; i++)
		cc_id_list_append(a, &l, args[i]);
	n = cc_node_new(a, N_CALL, AST_TYPE(a, a->syms[fs].ty)->base, loc);
	AST_NODE_AT(a, n)->a = cxx_gen_function_node(c, fs, loc);
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &l);
	AST_NODE_AT(a, n)->c = nargs;
	return n;
}

static uint32_t cxx_gen_to_void_pointer(cxx *c, uint32_t n)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));

	return cc_cast_insert(a, n, vp, cc_conversion_cast_kind(a, AST_NODE_AT(a, n)->ty, vp, n));
}

/*
 * throw x: the exception object is allocated by the runtime and initialized from x, then thrown with its
 * type info and its destructor.  throw alone: the exception being handled, again.
 */
VPRIV void cxx_gen_throw(cxx *c, uint32_t x, bool has_operand, uint32_t loc)
{
	cc *a = c->ast;
	qty t, vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), pt;
	uint32_t ti, tmp, alloc, target, init, args[3], dtor = 0, n;
	cxx_entity *d;

	if (!cxx_gen_on(c))
		return;
	c->enode = 0;
	if (!has_operand) {
		c->enode = cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 2), NULL, 0, loc);
		return;
	}
	if (!x)
		return;
	if (!cc_type_is_record(a, AST_NODE_AT(a, x)->ty) || AST_TYPE_KIND(a, AST_NODE_AT(a, x)->ty) == TY_ARRAY)
		x = cc_conversion_decay(a, x);	/* (an array, a function: a pointer is thrown) */
	t = QUNQ(AST_NODE_AT(a, x)->ty);
	ti = cxx_gen_typeinfo(c, t);
	if (!ti)
		return;
	tmp = cxx_gen_temporary(c, vp, loc);
	if (!tmp)
		return;
	n = cc_node_integer_new(a, (i128)AST_TYPE(a, t)->size, cc_type_basic(TY_ULONG), loc);
	alloc = cc_assignment_build(a, 0, cxx_gen_var_node(c, tmp, loc), cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 0), &n, 1, loc), loc);
	pt = cc_type_pointer(a, t);
	n = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
	target = cc_dereference_build(a, cc_cast_insert(a, n, pt, cc_conversion_cast_kind(a, vp, pt, n)), loc);
	if (cc_type_is_record(a, t)) {
		/* a class: copied or moved by its constructor (a prvalue: made there in the first place) */
		qty at[1] = { AST_NODE_AT(a, x)->ty };
		uint8_t lv[1] = { 1 };
		cc_id_list l;

		if (!(AST_NODE_AT(a, x)->flags & NF_LVALUE))
			return;
		if (!cxx_gen_prv_find(c, x))
			cxx_ctor_use(c, t, at, lv, 1, false);
		cc_id_list_init(&l);
		if (!cxx_gen_construct_at(c, target, t, &x, 1, &l, loc))
			return;
		init = cxx_gen_chain(c, &l, 0, loc);
		d = cxx_gen_effective_destructor(c, t, 0);
		if (cxx_gen_needs_destruction(c, t, 0)) {
			if (!d || d->fn_deleted || !cxx_gen_name(c, d) || !d->sym1 || (cxx_member_body_use(c, d), 0))
				return;
			dtor = cxx_gen_dtor_sym(d);
		}
	} else
		init = cc_assignment_build(a, 0, target, cc_node_rvalue(a, x), loc);
	args[0] = cc_node_rvalue(a, cxx_gen_var_node(c, tmp, loc));
	args[1] = cxx_gen_data_address(c, ti, 0);
	if (dtor)
		args[2] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, dtor, loc));
	else
		args[2] = cxx_gen_to_void_pointer(c, cc_node_integer_new(a, 0, cc_type_basic(TY_LONG), loc));
	n = cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 1), args, 3, loc);
	if (init)
		n = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc, init, n);
	c->enode = cc_node_new_operands(a, N_COMMA, cc_type_basic(TY_VOID), loc, alloc, n);
}

static const char *cxx_gen_symbol_asm_name(cxx *c, uint32_t s)
{
	cc *a = c->ast;
	const sym *y = &a->syms[s];

	return y->attrs && a->attr[y->attrs].asmname ? a->strs[a->attr[y->attrs].asmname - 1].s : NULL;
}

/* text at file scope, for the assembler as it is */
static void cxx_gen_file_asm(cxx *c, const char *text)
{
	cc *a = c->ast;
	uint32_t si = cxx_gen_text(c, text), ai = a->nasm++, n;

	memset(&a->asms[ai], 0, sizeof a->asms[ai]);
	a->asms[ai].tmpl = si + 1;
	n = cc_node_new(a, N_ASM, cc_type_basic(TY_VOID), 0);
	AST_NODE_AT(a, n)->a = ai;
	a->tlasm[a->ntlasm++] = n;
}

/*
 * A try block.  Its body was built as a function of its own (a closure's operator(), `call`, returning
 * int; `closure` is the object holding its captures).  The body is called through a small function
 * written out in assembly, which has the exception table: when an exception one of the handlers takes
 * comes out of the body, that function returns minus the handler's number (1..) with the exception at
 * *exc; else what the body returned (0).  Here: the call; its result's node (an int).  The handlers' type
 * info symbols: ti[] (0: catch (...)).  x86-64 only.  0: not built.
 */
VPRIV uint32_t cxx_gen_try(cxx *c, uint32_t closure, cxx_entity *call, const uint32_t *ti, uint32_t nhandlers, uint32_t exc, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };
	uint32_t serial = c->gen_try_serial++, fs, args[3];
	char name[64], *text;
	size_t cap = 4096 + 256 * (size_t)nhandlers, n = 0;
	attrs at;

	if (!cxx_gen_on(c) || a->o->target != PP_LINUX_X86_64 || !closure || !call->sym || !nhandlers || nhandlers > 60 ||
	    !(AST_NODE_AT(a, closure)->flags & NF_LVALUE))
		return 0;
	for (uint32_t k = 0; k < nhandlers; k++) {
		attrs used;

		if (ti[k] && !cxx_gen_symbol_asm_name(c, ti[k]))
			return 0;
		if (ti[k]) {	/* (named by the exception table, in assembly: kept whatever the code refers to) */
			memset(&used, 0, sizeof used);
			used.used = 1;
			cc_attribute_apply_symbol(a, &a->syms[ti[k]], &used);
		}
	}
	if (!exc)
		return 0;
	text = malloc(cap);
#define ASM(...) (n += (size_t)snprintf(text + n, cap - n, __VA_ARGS__))
	if (!c->gen_eh_pers) {
		c->gen_eh_pers = 1;
		ASM("\t.section\t.data.rel.ro,\"aw\",@progbits\n\t.p2align\t3\n.Lvcc_personality:\n\t.quad\t__gxx_personality_v0\n");
	}
	ASM("\t.text\n\t.type\t__vcc_try_%u,@function\n__vcc_try_%u:\n\t.cfi_startproc\n", serial, serial);
	ASM("\t.cfi_personality 0x9b, .Lvcc_personality\n\t.cfi_lsda 0x1b, .Lvcc_lsda_%u\n", serial);
	ASM("\tpushq\t%%rbx\n\t.cfi_def_cfa_offset 16\n\t.cfi_offset %%rbx, -16\n\tmovq\t%%rdx, %%rbx\n\tmovq\t%%rdi, %%rax\n\tmovq\t%%rsi, %%rdi\n");
	ASM(".Lvcc_cs_%u:\n\tcallq\t*%%rax\n.Lvcc_ce_%u:\n\tpopq\t%%rbx\n\t.cfi_def_cfa_offset 8\n\tretq\n", serial, serial);
	ASM(".Lvcc_lp_%u:\n\t.cfi_def_cfa_offset 16\n\tmovq\t%%rax, (%%rbx)\n\tmovl\t%%edx, %%eax\n\tnegl\t%%eax\n\tpopq\t%%rbx\n\t.cfi_def_cfa_offset 8\n\tretq\n", serial);
	ASM("\t.cfi_endproc\n.Lvcc_end_%u:\n\t.size\t__vcc_try_%u, .Lvcc_end_%u-__vcc_try_%u\n", serial, serial, serial, serial);
	/* the exception table: one call site, its actions (handler k: filter k, then the next), the types last first */
	ASM("\t.section\t.gcc_except_table,\"a\",@progbits\n.Lvcc_lsda_%u:\n\t.byte\t0xff\n\t.byte\t0x9b\n", serial);
	ASM("\t.uleb128 .Lvcc_tt_%u-.Lvcc_ttb_%u\n.Lvcc_ttb_%u:\n\t.byte\t0x01\n\t.uleb128 .Lvcc_cse_%u-.Lvcc_csb_%u\n.Lvcc_csb_%u:\n", serial, serial, serial, serial, serial, serial);
	ASM("\t.uleb128 .Lvcc_cs_%u-__vcc_try_%u\n\t.uleb128 .Lvcc_ce_%u-.Lvcc_cs_%u\n\t.uleb128 .Lvcc_lp_%u-__vcc_try_%u\n\t.uleb128 1\n.Lvcc_cse_%u:\n",
	    serial, serial, serial, serial, serial, serial, serial);
	for (uint32_t k = 0; k < nhandlers; k++)
		ASM("\t.byte\t%u\n\t.byte\t%u\n", k + 1, k + 1 < nhandlers ? 1u : 0u);
	for (uint32_t k = nhandlers; k-- > 0;) {
		if (ti[k])
			ASM("\t.long\t.Lvcc_ti_%u_%u-.\n", serial, k);
		else
			ASM("\t.long\t0\n");
	}
	ASM(".Lvcc_tt_%u:\n\t.section\t.data.rel.ro,\"aw\",@progbits\n\t.p2align\t3\n", serial);
	for (uint32_t k = 0; k < nhandlers; k++)
		if (ti[k])
			ASM(".Lvcc_ti_%u_%u:\n\t.quad\t%s\n", serial, k, cxx_gen_symbol_asm_name(c, ti[k]));
	ASM("\t.text");
#undef ASM
	cxx_gen_file_asm(c, text);
	free(text);
	snprintf(name, sizeof name, " __vcc_try_%u", serial);
	fs = cc_symbol_new(a, S_FUNC, vintern_cstr("__vcc_try"), cxx_type_function(c, cc_type_basic(TY_INT), ps, 3, false), loc);
	a->syms[fs].flags |= SF_GLOBAL;
	a->syms[fs].depth = 0;
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name + 1) + 1;
	cc_attribute_apply_symbol(a, &a->syms[fs], &at);
	args[0] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, call->sym, loc));
	args[1] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, closure, loc));
	args[2] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, cxx_gen_var_node(c, exc, loc), loc));
	return cxx_gen_runtime_call(c, fs, args, 3, loc);
}

/* a scratch variable of the block being built (0: no block) */
VPRIV uint32_t cxx_gen_scratch(cxx *c, qty t, uint32_t loc)
{
	return cxx_gen_on(c) ? cxx_gen_temporary(c, t, loc) : 0;
}

/* a try block's outlined body, then "return 0" (it fell off its end) */
VPRIV uint32_t cxx_gen_return_zero(cxx *c, uint32_t body, uint32_t loc)
{
	cc *a = c->ast;
	cc_id_list l;

	if (!body)
		return 0;
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, body);
	cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), 0, 0));
	return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
}

/* the try statement: code = the call of the body through its trampoline; then the handler it names, if any */
VPRIV uint32_t cxx_gen_try_statement(cxx *c, uint32_t call, const uint32_t *handlers, uint32_t nhandlers, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t code = cxx_gen_temporary(c, cc_type_basic(TY_INT), loc), chain = 0;
	cc_id_list l;

	if (!code || !call)
		return 0;
	for (uint32_t k = nhandlers; k-- > 0;) {
		uint32_t cond = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, cc_node_rvalue(a, cxx_gen_var_node(c, code, loc)),
						cc_node_integer_new(a, -(i128)(k + 1), cc_type_basic(TY_INT), loc), loc);

		chain = cc_statement_new(a, N_IF, loc, cc_condition_scalar(a, cond, "statement"), handlers[k], chain);
	}
	cc_id_list_init(&l);
	cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, cxx_gen_var_node(c, code, loc), call, loc), 0, 0));
	cc_id_list_append(a, &l, chain);
	return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
}

/*
 * A handler's start, into `list` (the handler's block): the exception is caught (__cxa_begin_catch, ended
 * by __cxa_end_catch when the block is left, any way but by an exception), and the handler's variable, of
 * the declared type decl_type (CXX_TY_UNKNOWN: none), is made: *var its symbol.  False: not built.
 */
/* a local's declaration with its initializer, into `list` */
static void cxx_gen_declare_with(cxx *c, cc_id_list *list, uint32_t s, uint32_t init, uint32_t loc)
{
	cc *a = c->ast;

	a->syms[s].sc = SC_AUTO;
	if (init) {
		a->syms[s].body = init;
		a->syms[s].flags |= SF_DEFINED;
	}
	cc_id_list_append(a, list, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, init));
}

VPRIV uint32_t cxx_gen_catch_begin(cxx *c, uint32_t exc, qty decl_type, uint32_t *var, cc_id_list *list, uint32_t loc)
{
	cc *a = c->ast;
	qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID));
	uint32_t obj = cc_symbol_new(a, S_VAR, 0, vp, loc), guard = cc_symbol_new(a, S_VAR, 0, cc_type_basic(TY_CHAR), loc), n, arg, s, init;
	attrs at;

	*var = 0;
	arg = cc_node_rvalue(a, cxx_gen_var_node(c, exc, loc));
	cxx_gen_declare_with(c, list, obj, cxx_gen_runtime_call(c, cxx_gen_eh_symbol(c, 3), &arg, 1, loc), loc);
	memset(&at, 0, sizeof at);
	at.cleanup = cxx_gen_eh_symbol(c, 4);
	cc_attribute_apply_symbol(a, &a->syms[guard], &at);
	cxx_gen_declare_with(c, list, guard, 0, loc);
	if (decl_type == CXX_TY_UNKNOWN)
		return 1;
	n = cc_node_rvalue(a, cxx_gen_var_node(c, obj, loc));
	if (cxx_gen_is_reference(c, decl_type)) {
		/* T &e: the exception object itself */
		s = cc_symbol_new(a, S_VAR, 0, decl_type, loc);
		init = cc_cast_insert(a, n, decl_type, cc_conversion_cast_kind(a, vp, decl_type, n));
	} else if (AST_TYPE_KIND(a, decl_type) == TY_PTR) {
		/* a pointer: the runtime hands the pointer's value */
		s = cc_symbol_new(a, S_VAR, 0, QUNQ(decl_type), loc);
		init = cc_cast_insert(a, n, QUNQ(decl_type), cc_conversion_cast_kind(a, vp, QUNQ(decl_type), n));
	} else if (cxx_gen_type_ok(c, QUNQ(decl_type)) || (cc_type_is_record(a, decl_type) && cxx_gen_trivial_abi(c, decl_type, 0) == 1)) {
		/* a copy of a scalar, or of a class copied as its bytes */
		qty pt = cc_type_pointer(a, QUNQ(decl_type));

		s = cc_symbol_new(a, S_VAR, 0, QUNQ(decl_type), loc);
		init = cc_node_rvalue(a, cc_dereference_build(a, cc_cast_insert(a, n, pt, cc_conversion_cast_kind(a, vp, pt, n)), loc));
	} else
		return 0;	/* (a class copied by its constructor: not built yet) */
	cxx_gen_declare_with(c, list, s, init, loc);
	*var = s;
	return 1;
}

/* ---- statements ---------------------------------------------------------------------------- */

/* this statement's node (0: an empty statement); cxx_statement_parse checks that its own case said so */
VPRIV void cxx_gen_statement(cxx *c, uint32_t node)
{
	c->snode = node;
	c->sdone = c->sdepth;
}

VPRIV uint32_t cxx_gen_new_statement(cxx *c, int op, uint32_t loc, uint32_t x, uint32_t y, uint32_t z)
{
	return cc_statement_new(c->ast, op, loc, x, y, z);
}

/*
 * for (decl : range) over an array (or a braced list, made an array first): { E *p = range; for (long i = 0;
 * i < N; i++) { decl = p[i]; body } }.  begin/end of a class: not built yet.  The parts as the parser has
 * them: elems/nelems for a braced list (else range, an lvalue array), the variable's symbol made here.
 */
VPRIV bool cxx_gen_range_for_begin(cxx *c, cxx_gen_range *r, uint32_t range, const uint32_t *elems, uint32_t nelems, qty elem, uint32_t loc)
{
	cc *a = c->ast;
	qty lt = cc_type_basic(TY_LONG), et, pt;
	uint32_t arr, n;

	memset(r, 0, sizeof *r);
	if (!cxx_gen_on(c) || !c->gen_block)
		return false;
	cc_id_list_init(&r->pre);
	r->outer = c->gen_block;
	if (elems) {
		/* a braced list: a local array of its elements, converted to their type */
		if (!nelems || elem == CXX_TY_UNKNOWN || !cxx_gen_type_ok(c, elem))
			return false;
		et = QUNQ(elem);
		arr = cxx_gen_local(c, 0, cc_type_array(a, et, nelems, 0), loc);
		if (!arr)
			return false;
		c->gen_block = &r->pre;
		cxx_gen_local_declare(c, arr, 0, false, loc);
		for (uint32_t i = 0; i < nelems; i++) {
			uint32_t x = elems[i] ? cc_conversion_apply(a, cc_conversion_decay(a, cc_node_rvalue(a, elems[i])), et) : 0, slot;

			if (!x)
				goto fail;
			slot = cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_conversion_decay(a, cxx_gen_var_node(c, arr, loc)),
								   cc_node_integer_new(a, i, lt, loc), loc), loc);
			cc_id_list_append(a, &r->pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, slot, x, loc), 0, 0));
		}
		range = cxx_gen_var_node(c, arr, loc);
		n = nelems;
	} else {
		const ctype *x;

		if (!range || !(AST_NODE_AT(a, range)->flags & NF_LVALUE) || AST_TYPE_KIND(a, AST_NODE_AT(a, range)->ty) != TY_ARRAY)
			return false;
		x = AST_TYPE(a, AST_NODE_AT(a, range)->ty);
		if ((x->flags & TF_INCOMPLETE) || !x->n || !cxx_gen_object_type_ok(c, x->base))
			return false;
		et = x->base, n = (uint32_t)x->n;
		c->gen_block = &r->pre;
	}
	pt = cc_type_pointer(a, et);
	r->p = cxx_gen_local(c, 0, pt, loc);
	r->i = cxx_gen_local(c, 0, lt, loc);
	if (!r->p || !r->i)
		goto fail;
	cxx_gen_local_declare(c, r->p, cc_conversion_decay(a, range), true, loc);
	cxx_gen_local_declare(c, r->i, cc_node_integer_new(a, 0, lt, loc), true, loc);
	r->n = n, r->loc = loc, r->ok = true;
	cc_id_list_init(&r->body);
	c->gen_block = &r->body;
	return true;
fail:
	c->gen_block = r->outer;
	return false;
}

VPRIV uint32_t cxx_gen_var_node_public(cxx *c, uint32_t s, uint32_t loc)
{
	return cxx_gen_var_node(c, s, loc);
}

/* !x, as a node (0: not built) */
VPRIV uint32_t cc_unary_build_public(cxx *c, int op, uint32_t x, uint32_t loc)
{
	uint32_t saved = c->enode, n;

	c->enode = 0;
	cxx_gen_unary(c, op, x, loc);
	n = c->enode;
	c->enode = saved;
	return n;
}

/* the element the loop is at (an lvalue): p[i] */
VPRIV uint32_t cxx_gen_range_for_element(cxx *c, cxx_gen_range *r)
{
	cc *a = c->ast;

	if (r->cls)
		return r->elem;

	return cc_dereference_build(a, cc_binary_build(a, LEX_TOKEN_PLUS, cc_node_rvalue(a, cxx_gen_var_node(c, r->p, r->loc)),
						       cc_node_rvalue(a, cxx_gen_var_node(c, r->i, r->loc)), r->loc), r->loc);
}

/* the body parsed (c->snode): the loop, as the statement */
VPRIV void cxx_gen_range_for_end(cxx *c, cxx_gen_range *r)
{
	cc *a = c->ast;
	uint32_t cond, step, body;
	cc_id_list sb;

	c->gen_block = r->outer;
	if (!r->ok || !cxx_gen_on(c))
		return;
	if (c->snode)
		cc_id_list_append(a, &r->body, c->snode);
	body = cc_statement_new(a, N_BLOCK, r->loc, 0, cc_id_list_freeze(a, &r->body), r->body.n);
	if (r->cls)
		cond = r->cond, step = r->step;
	else {
		cond = cc_binary_build(a, LEX_TOKEN_LESS, cc_node_rvalue(a, cxx_gen_var_node(c, r->i, r->loc)),
				       cc_node_integer_new(a, r->n, cc_type_basic(TY_LONG), r->loc), r->loc);
		step = cc_assignment_build(a, LEX_TOKEN_PLUS, cxx_gen_var_node(c, r->i, r->loc), cc_node_integer_new(a, 1, cc_type_basic(TY_LONG), r->loc), r->loc);
	}
	cc_id_list_init(&sb);
	cc_id_list_append(a, &sb, step);
	cc_id_list_append(a, &sb, body);
	cc_id_list_append(a, &r->pre, cc_statement_new(a, N_FOR, r->loc, 0, cond, cc_id_list_freeze(a, &sb)));
	cxx_gen_statement(c, cc_statement_new(a, N_BLOCK, r->loc, 0, cc_id_list_freeze(a, &r->pre), r->pre.n));
}

/* a condition: the expression's node as a scalar truth value (0: not built) */
VPRIV uint32_t cxx_gen_condition(cxx *c, uint32_t e)
{
	if (!cxx_gen_on(c))
		return 0;
	if (e && cc_type_is_record(c->ast, AST_NODE_AT(c->ast, e)->ty))
		e = cxx_gen_convert_class(c, e, cc_type_basic(TY_BOOL), 0);	/* a class as a condition: its operator bool */
	if (!e || cc_type_is_record(c->ast, AST_NODE_AT(c->ast, e)->ty)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cc_condition_scalar(c->ast, cc_conversion_decay(c->ast, e), "statement");
}

/* switch (e): the value switched on */
VPRIV uint32_t cxx_gen_switch_value(cxx *c, uint32_t e)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c))
		return 0;
	if (!e) {
		cxx_gen_fail(c);
		return 0;
	}
	e = cc_node_rvalue(a, e);
	if (AST_TYPE_KIND(a, AST_NODE_AT(a, e)->ty) == TY_ENUM)
		e = cc_cast_insert(a, e, QUNQ(AST_TYPE(a, AST_NODE_AT(a, e)->ty)->base), CK_NOP);
	if (!cc_type_is_integer(a, AST_NODE_AT(a, e)->ty)) {
		cxx_gen_fail(c);
		return 0;
	}
	return cc_conversion_apply(a, e, cc_node_promoted_type(a, e));
}

/* case lo ... hi: stmt */
VPRIV uint32_t cxx_gen_case(cxx *c, int64_t lo, int64_t hi, uint32_t stmt, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	a->ipool[a->nipool] = lo;
	a->ipool[a->nipool + 1] = hi;
	n = cc_statement_new(a, N_CASE, loc, a->nipool, a->nipool + 1, stmt);
	a->nipool += 2;
	return n;
}

/* return e; (e: 0 for return;) converted to the function's return type */
VPRIV uint32_t cxx_gen_return(cxx *c, uint32_t e, bool has_value, uint32_t loc)
{
	cc *a = c->ast;

	if (!cxx_gen_on(c))
		return 0;
	if (c->gen_lambda && c->gen_lambda->is_try) {
		cxx_gen_fail(c);	/* (a return out of a try block: not built yet) */
		return 0;
	}
	if (has_value && e && AST_TYPE_KIND(a, c->gen_ret) == TY_VOID && AST_TYPE_KIND(a, AST_NODE_AT(a, e)->ty) == TY_VOID) {
		/* return f(); in a void function (C++ allows it, C does not): f(), then return */
		cc_id_list l;

		cc_id_list_init(&l);
		cc_id_list_append(a, &l, cc_statement_new(a, N_EXPR, loc, e, 0, 0));
		cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, 0, 0, 0));
		return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
	}
	if (has_value && (!e || AST_TYPE_KIND(a, c->gen_ret) == TY_VOID)) {
		cxx_gen_fail(c);
		return 0;
	}
	if (has_value && c->gen_sret) {
		/* a class returned by address: constructed in the object the caller gave, whose address is returned */
		qty t = QUNQ(c->gen_ret), at[1];
		uint8_t lv[1];
		const node *en = AST_NODE_AT(a, e);
		cc_id_list l;
		uint32_t target = cc_dereference_build(a, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)), loc);

		if (QUNQ(en->ty) != t || !(en->flags & NF_LVALUE)) {
			cxx_gen_fail(c);	/* (a conversion to the class: not built yet) */
			return 0;
		}
		/* a local variable named alone is moved from; any other object is copied */
		at[0] = en->ty;
		lv[0] = !(en->op == N_VAR && a->syms[en->a].sc == SC_AUTO && !cxx_gen_is_reference(c, a->syms[en->a].ty));
		if (!cxx_gen_prv_find(c, e))
			cxx_ctor_use(c, t, at, lv, 1, false);
		cc_id_list_init(&l);
		if (!cxx_gen_construct_at(c, target, t, &e, 1, &l, loc)) {
			cxx_gen_fail(c);
			return 0;
		}
		cc_id_list_append(a, &l, cc_statement_new(a, N_RETURN, loc, cc_node_rvalue(a, cxx_gen_var_node(c, c->gen_sret, loc)), 0, 0));
		return cc_statement_new(a, N_BLOCK, loc, 0, cc_id_list_freeze(a, &l), l.n);
	}
	if (has_value) {
		e = cxx_gen_bind(c, e, c->gen_ret, loc);
		if (!e) {
			cxx_gen_fail(c);
			return 0;
		}
	}
	return cc_statement_new(a, N_RETURN, loc, e, 0, 0);
}

/* a local variable of the body: its symbol (0: a type not generated yet) */
VPRIV uint32_t cxx_gen_local(cxx *c, uint32_t name, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t s;

	if (!cxx_gen_on(c))
		return 0;
	if (!cxx_gen_object_type_ok(c, t) && !(cxx_gen_is_reference(c, t) && cxx_gen_type_ok(c, t))) {
		cxx_gen_fail(c);
		return 0;
	}
	s = cc_symbol_new(a, S_VAR, name, t, loc);
	a->syms[s].sc = SC_AUTO;
	return s;
}

/* a local variable in static storage (static or constexpr): C's function-scope static, initialized by a
   constant (cxx_gen_local_declare checks it) */
VPRIV uint32_t cxx_gen_local_static(cxx *c, uint32_t name, qty t, uint32_t loc)
{
	uint32_t s = cxx_gen_local(c, name, t, loc);

	if (s)
		c->ast->syms[s].sc = SC_STATIC;
	return s;
}

VPRIV bool cxx_gen_needs_construction_public(cxx *c, qty t)
{
	return cxx_gen_needs_construction(c, t, 0) || cxx_gen_needs_destruction(c, t, 0);
}

/* its declaration statement, with the initializer's node (0: none), appended to the block being built */
VPRIV void cxx_gen_local_declare(cxx *c, uint32_t s, uint32_t init, bool has_init, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t n;

	if (!cxx_gen_on(c) || !s)
		return;
	if (has_init && !init) {
		cxx_gen_fail(c);
		return;
	}
	if (!c->gen_block) {
		cxx_gen_fail(c);	/* (a declaration that is not a block's statement: a condition's) */
		return;
	}
	if (has_init && AST_NODE_AT(a, init)->op != N_INIT && !cxx_gen_type_ok(c, a->syms[s].ty) &&
	    !(AST_NODE_AT(a, init)->op == N_STR && AST_TYPE_KIND(a, a->syms[s].ty) == TY_ARRAY)) {	/* (char s[] = "text": C's) */
		cxx_gen_fail(c);	/* (an array from something that is not a list) */
		return;
	}
	if (!has_init && cxx_gen_is_reference(c, a->syms[s].ty)) {
		cxx_gen_fail(c);
		return;
	}
	if (has_init) {
		if (AST_NODE_AT(a, init)->op != N_INIT && !(AST_NODE_AT(a, init)->op == N_STR && AST_TYPE_KIND(a, a->syms[s].ty) == TY_ARRAY))
			init = cxx_gen_bind(c, init, a->syms[s].ty, loc);	/* (char s[] = "text": the literal, as C's initializer takes it) */
		if (!init || (a->syms[s].sc == SC_STATIC && !cxx_gen_static_ok(a, a->syms[s].ty, init))) {
			cxx_gen_fail(c);	/* (a static local's dynamic initializer: its guard, not built yet) */
			return;
		}
		a->syms[s].body = init;
		a->syms[s].flags |= SF_DEFINED;
	}
	n = cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, has_init ? init : 0);
	cc_id_list_append(a, (cc_id_list *)c->gen_block, n);
}

/*
 * T a[N] = { x, y, ... } with scalar elements: the initializer list's node from the last braced list's
 * elements (0: not built: nested lists, an array of unknown bound, too many elements).
 */
VPRIV uint32_t cxx_gen_array_initializer(cxx *c, qty t, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *x;
	cc_id_list v;
	uint32_t n;

	if (!cxx_gen_on(c) || t == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, t) != TY_ARRAY || c->brace_n == UINT32_MAX)
		return 0;
	x = AST_TYPE(a, t);
	if (!x->n || c->brace_n > x->n || !cxx_gen_type_ok(c, x->base))
		return 0;
	cc_id_list_init(&v);
	for (uint32_t i = 0; i < c->brace_n; i++) {
		cc_id_list_append(a, &v, i);
		cc_id_list_append(a, &v, i);
		cc_id_list_append(a, &v, cc_conversion_apply(a, cc_conversion_decay(a, c->brace_nodes[i]), QUNQ(x->base)));
	}
	n = cc_node_new(a, N_INIT, t, loc);
	AST_NODE_AT(a, n)->aux = 1;
	AST_NODE_AT(a, n)->c = v.n / 3;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &v);
	return n;
}

/* ---- variables at namespace scope ------------------------------------------------------------ */

/*
 * A variable defined at namespace scope: before its initializer is parsed (true: its node is wanted)
 * and after (init: the node, 0 with has_init for one not built: the variable is then left undefined).
 * Without an initializer it is zero-initialized (a tentative definition, as in C).
 */
VPRIV bool cxx_gen_global_begin(cxx *c, const cxx_entity *v)
{
	cc *a = c->ast;
	cxx_entity *f;
	char nm[64];
	attrs at;

	if (!a->o->emit || c->tentative || c->template_depth || c->instantiating || c->gen || !v || !v->sym || !(v->global_ns || v->mangled_var) ||
	    !cxx_gen_object_type_ok(c, v->ty))
		return false;
	/* The initializer is built inside a function of its own: if it turns out not to be a constant, that
	   function runs it when the program starts (.init_array), in the order of the definitions. */
	f = calloc(1, sizeof *f);
	f->kind = CXX_ENTITY_VALUE, f->name = vintern_cstr("__vcc_init"), f->pack_index = -1;
	f->ty = cxx_type_function(c, cc_type_basic(TY_VOID), NULL, 0, false);
	f->fn_internal = 1, f->mangled = 1;
	f->sym = cc_symbol_new(a, S_FUNC, f->name, f->ty, 0);
	a->syms[f->sym].depth = 0;
	a->syms[f->sym].sc = SC_STATIC;
	snprintf(nm, sizeof nm, "__vcc_init_%u", c->ginit_serial++);
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, nm) + 1;
	at.constructor = 1, at.used = 1;	/* (run at start-up: the emitter's .init_array entry, when it is defined) */
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	if (!cxx_gen_function_begin(c, f, c->scope, NULL, 0)) {
		free(f);
		return false;
	}
	c->ginit_fn = f;
	c->ginit_list = calloc(1, sizeof(cc_id_list));
	cc_id_list_init(c->ginit_list);
	c->gen_block = c->ginit_list;
	return true;
}

/* the start-up function is done: kept (true) with its statements as its body and an .init_array entry, or dropped */
static bool cxx_gen_global_finish(cxx *c, bool keep)
{
	cc *a = c->ast;
	cxx_entity *f = c->ginit_fn;
	cc_id_list *l = c->ginit_list;
	uint32_t body = 0;
	bool defined;

	c->ginit_fn = NULL, c->ginit_list = NULL;
	if (!f)
		return false;
	if (keep && cxx_gen_on(c) && l->n)
		body = cc_statement_new(a, N_BLOCK, 0, 0, cc_id_list_freeze(a, l), l->n);
	else
		c->gen_failed = 1;
	cxx_gen_function_end(c, f, body);
	free(l);
	defined = (a->syms[f->sym].flags & SF_DEFINED) != 0;
	if (!defined && c->stats.functions)
		c->stats.functions--;	/* (not a function of the program's: a constant initializer needs none) */
	return defined;
}

/* the variable itself, zero until the start-up function has run (not const in the object: it is written then) */
static void cxx_gen_global_zeroed(cxx *c, const cxx_entity *v)
{
	cc *a = c->ast;
	sym *y = &a->syms[v->sym];

	if (y->flags & (SF_DEFINED | SF_TENTATIVE))
		return;
	y->ty = QUNQ(y->ty);
	y->flags |= SF_TENTATIVE;
	a->toplevel[a->ntoplevel++] = v->sym;
}

/* x as an lvalue of its type without const (an initialization writes it) */
static uint32_t cxx_gen_unqualified(cxx *c, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	qty t = AST_NODE_AT(a, x)->ty, pt;
	uint32_t addr;

	if (t == QUNQ(t))
		return x;
	pt = cc_type_pointer(a, QUNQ(t));
	addr = cc_address_of_build(a, x, loc);
	return cc_dereference_build(a, cc_cast_insert(a, addr, pt, cc_conversion_cast_kind(a, AST_NODE_AT(a, addr)->ty, pt, addr)), loc);
}

/*
 * A namespace-scope object of class type t, initialized from args (cxx_ctor_use was called for it): built
 * by the start-up function, and its destructor registered to run at exit (__cxa_atexit).
 */
VPRIV void cxx_gen_global_object(cxx *c, const cxx_entity *v, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *picked = c->ctor_picked, *dtor = NULL;
	bool found = c->ctor_found, none = c->ctor_none, ok = c->ginit_fn && cxx_gen_on(c);
	bool needs = ok && cxx_gen_needs_destruction(c, t, 0);

	if (ok && needs) {
		dtor = cxx_gen_effective_destructor(c, t, 0);
		ok = dtor && !dtor->fn_deleted && cxx_gen_name(c, dtor) && dtor->sym1 && (cxx_member_body_use(c, dtor), 1);
	}
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	ok = ok && cxx_gen_construct_at(c, cxx_gen_var_node(c, v->sym, loc), QUNQ(t), args, nargs, c->ginit_list, loc);
	if (ok && dtor) {
		/* __cxa_atexit(destructor, &object, &__dso_handle) */
		qty vp = cc_type_pointer(a, cc_type_basic(TY_VOID)), ps[3] = { vp, vp, vp };
		uint32_t ar[3];

		if (!c->gen_atexit_sym) {
			c->gen_atexit_sym = cc_symbol_new(a, S_FUNC, vintern_cstr("__cxa_atexit"), cxx_type_function(c, cc_type_basic(TY_INT), ps, 3, false), 0);
			a->syms[c->gen_atexit_sym].flags |= SF_GLOBAL;
			a->syms[c->gen_atexit_sym].depth = 0;
			c->gen_dso_sym = cxx_gen_data_symbol(c, "__dso_handle", 0);
		}
		ar[0] = cxx_gen_to_void_pointer(c, cxx_gen_function_node(c, cxx_gen_dtor_sym(dtor), loc));
		ar[1] = cxx_gen_to_void_pointer(c, cc_address_of_build(a, cxx_gen_var_node(c, v->sym, loc), loc));
		ar[2] = cxx_gen_data_address(c, c->gen_dso_sym, 0);
		cc_id_list_append(a, c->ginit_list, cc_statement_new(a, N_EXPR, loc, cxx_gen_runtime_call(c, c->gen_atexit_sym, ar, 3, loc), 0, 0));
	}
	if (cxx_gen_global_finish(c, ok && ((cc_id_list *)c->ginit_list)->n))
		cxx_gen_global_zeroed(c, v);
	else if (ok)
		cxx_gen_global_zeroed(c, v);	/* (nothing to run: a class with no construction, zeroed) */
}

VPRIV void cxx_gen_global_end(cxx *c, const cxx_entity *v, uint32_t init, bool has_init)
{
	cc *a = c->ast;
	sym *y = &a->syms[v->sym];
	bool failed = c->gen_failed || !cxx_gen_on(c);
	cc_id_list *l = c->ginit_list;

	if (!c->ginit_fn) {
		c->gen = 0;
		return;
	}
	if (y->flags & (SF_DEFINED | SF_TENTATIVE)) {
		cxx_gen_global_finish(c, false);
		return;
	}
	if (!has_init) {
		if (cc_type_is_record(a, y->ty) && AST_TYPE_KIND(a, y->ty) != TY_ARRAY && cxx_gen_needs_construction(c, y->ty, 0)) {
			/* T x; of a class with a constructor: default-initialized at start-up */
			cxx_ctor_use(c, y->ty, NULL, NULL, 0, false);
			cxx_gen_global_object(c, v, y->ty, NULL, 0, 0);
			return;
		}
		if (cc_type_is_record(a, y->ty) && cxx_gen_needs_destruction(c, y->ty, 0)) {
			cxx_gen_global_object(c, v, y->ty, NULL, 0, 0);
			return;
		}
		cxx_gen_global_finish(c, false);
		y = &a->syms[v->sym];
		y->flags |= SF_TENTATIVE;
		a->toplevel[a->ntoplevel++] = v->sym;
		return;
	}
	if (failed || !init) {
		cxx_gen_global_finish(c, false);	/* (an initializer not built yet: the variable stays a declaration) */
		return;
	}
	if (!l->n) {
		/* a constant initializer: in the object file */
		uint32_t k = init;

		if (AST_NODE_AT(a, k)->op != N_INIT)
			k = cxx_gen_type_ok(c, v->ty) && !cc_type_is_record(a, AST_NODE_AT(a, k)->ty) ? cc_conversion_apply(a, cc_conversion_decay(a, k), QUNQ(y->ty)) : 0;
		if (k && cxx_gen_static_ok(a, y->ty, k)) {
			cxx_gen_global_finish(c, false);
			y = &a->syms[v->sym];
			y->body = k;
			y->flags |= SF_DEFINED;
			a->toplevel[a->ntoplevel++] = v->sym;
			return;
		}
	}
	/* not a constant: var = init when the program starts */
	if (AST_NODE_AT(a, init)->op == N_INIT || cxx_gen_is_reference(c, v->ty) || !cxx_gen_type_ok(c, v->ty) || cc_type_is_record(a, v->ty)) {
		cxx_gen_global_finish(c, false);	/* (a list, a reference, a class by value: not built yet) */
		return;
	}
	init = cxx_gen_bind(c, init, QUNQ(v->ty), 0);
	if (!init) {
		cxx_gen_global_finish(c, false);
		return;
	}
	cc_id_list_append(a, l, cc_statement_new(a, N_EXPR, 0, cc_assignment_build(a, 0, cxx_gen_unqualified(c, cxx_gen_var_node(c, v->sym, 0), 0), init, 0), 0, 0));
	if (cxx_gen_global_finish(c, true))
		cxx_gen_global_zeroed(c, v);
}

/* ---- functions ----------------------------------------------------------------------------- */

/*
 * A function definition at namespace scope, before its body: its parameters' symbols, bound to the
 * parameter entities of the body's scope fs.  False: not a function code is generated for yet.
 */
/* VCC_CXX_GEN=3: why a function definition was not even started (cxx_gen_function_begin) */
#define CXX_BEGIN_WHY() do { \
		if (getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) >= 3 && f && !c->tentative && !c->template_depth && !c->unevaluated && \
		    !(f->sym && (c->ast->syms[f->sym].flags & SF_DEFINED))) \
			fprintf(stderr, "no begin (codegen.c:%d): %.*s\n", __LINE__, (int)vintern_ents[f->name].len, vintern_ents[f->name].s); \
	} while (0)

VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list psyms;
	cxx_param own[32];

	/* (the parser's parameter records are its scratch: completing a class below parses declarations of its own) */
	if (nps > 32)
		{ CXX_BEGIN_WHY(); return false; }
	memcpy(own, ps, nps * sizeof *ps);
	ps = own;

	if (!a->o->emit || c->tentative || c->template_depth || c->unevaluated || !f || f->ty == CXX_TY_UNKNOWN ||
	    AST_TYPE_KIND(a, f->ty) != TY_FUNC || (f->sym && (a->syms[f->sym].flags & SF_DEFINED)) || c->gen_stack_depth > 48)
		{ CXX_BEGIN_WHY(); return false; }
	ft = AST_TYPE(a, f->ty);
	if ((ft->flags & TF_VARIADIC) || ft->n != nps || (!cxx_gen_value_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		{ CXX_BEGIN_WHY(); return false; }
	for (uint32_t i = 0; i < nps; i++)	/* (the parser's record of a parameter may lack its type: the function type has it) */
		if (!cxx_gen_value_ok(c, ps[i].ty != CXX_TY_UNKNOWN ? ps[i].ty : a->params[ft->aux + i]))
			{ CXX_BEGIN_WHY(); return false; }
	if (!cxx_gen_name(c, f) || !f->sym)
		{ CXX_BEGIN_WHY(); return false; }
	{
		/* Two definitions with one symbol name: char16_t and unsigned short (and wchar_t, char32_t,
		   char8_t) are one type here for now, so overloads on them mangle alike.  The first has the code. */
		const sym *y = &a->syms[f->sym];
		const char *nm = y->attrs && a->attr[y->attrs].asmname ? a->strs[a->attr[y->attrs].asmname - 1].s : NULL;

		if (nm && cxx_gen_name_defined(c, nm))
			{ CXX_BEGIN_WHY(); return false; }
	}
	ft = AST_TYPE(a, f->ty);
	/* (from here the body is being built: what was, if anything, is set aside until cxx_gen_function_end) */
	cxx_gen_push(c);
	c->gen = 0, c->gen_failed = 0, c->gen_this = 0, c->gen_pre = NULL, c->gen_level = c->instantiating;
	c->gen_sret = 0, c->gen_notemp = 0, c->gen_block_temps = 0;
	c->gen_lambda = NULL;	/* (a lambda's operator(): set by its caller after this) */
	cc_scope_open(a);
	cc_id_list_init(&psyms);
	if (cxx_gen_by_address(c, ft->base)) {
		/* the result object's address, first of all */
		uint32_t s = cc_symbol_new(a, S_VAR, 0, cc_type_pointer(a, QUNQ(ft->base)), a->syms[f->sym].loc);

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		c->gen_sret = s;
	}
	if (cxx_gen_has_this(f)) {
		uint32_t s = cc_symbol_new(a, S_VAR, vintern_cstr("this"), cxx_gen_this_type(c, f), a->syms[f->sym].loc);

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		c->gen_this = s;
	}
	for (uint32_t i = 0; i < nps; i++) {
		/* (a class passed by address: the parameter names the caller's temporary, as a reference would) */
		qty pin = ps[i].ty != CXX_TY_UNKNOWN ? ps[i].ty : a->params[AST_TYPE(a, f->ty)->aux + i];
		qty pty = cxx_gen_by_address(c, pin) ? cxx_type_reference(c, QUNQ(pin), false) : QUNQ(pin);
		uint32_t s = cc_symbol_new(a, S_VAR, ps[i].name > 1 ? ps[i].name : 0, pty, a->syms[f->sym].loc);
		cxx_entity *pe = ps[i].name > 1 ? cxx_lookup_in(c, fs, ps[i].name, 0) : NULL;

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		if (c->gen_pack_entity && i >= c->gen_pack_from && i < c->gen_pack_from + c->gen_pack_n)
			c->gen_pack_entity->pack_syms[i - c->gen_pack_from] = s;	/* (a pack's element: found through the pack) */
		else if (pe)
			pe->sym = s;
	}
	c->gen_psyms = cc_id_list_freeze(a, &psyms), c->gen_npsyms = psyms.n;
	c->gen_pre = calloc(1, sizeof(cc_id_list));
	cc_id_list_init(c->gen_pre);
	c->ngen_init_names = 0, c->ngen_init_bases = 0, c->gen_delegates = 0, c->gen_returns = 0, c->gen_why = 0;
	c->gen_ret = ft->base;
	a->fn = f->sym;
	c->gen = 1;
	c->snode = 0, c->sdone = 0, c->sdepth = 0, c->gen_block = NULL;
	if (cxx_gen_is_special(f) && vintern_ents[f->name].s[1] == 'c')
		c->gen_block = c->gen_pre;	/* (a constructor: its initializers' temporaries are declared among them) */
	return true;
}

/* ... after its body (body: the compound statement's node): defined, for emit_tu, if all of it was built */
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body)
{
	cc *a = c->ast;
	bool ok = c->gen && !c->gen_failed && body;
	cc_id_list *pre = c->gen_pre;

	if (!ok && c->gen && getenv("VCC_CXX_GEN") && atoi(getenv("VCC_CXX_GEN")) == 4)	/* VCC_CXX_GEN=4: each function without code, why */
		fprintf(stderr, "nofn %.*s: %s\n", (int)vintern_ents[f->name].len, vintern_ents[f->name].s, cxx_gen_why), cxx_gen_why[0] = 0;

	if (ok && cxx_gen_is_special(f) && c->gen_this) {
		const vident *fv = &vintern_ents[f->name];
		uint32_t bloc = AST_NODE_AT(a, body)->loc;
		cc_id_list all;

		cc_id_list_init(&all);
		if (fv->s[1] == 'c') {
			/* a constructor: the bases and members its initializers did not name, the initializers, its body
			   (members' default initializers: not built yet) */
			ok = cxx_gen_constructor_defaults(c, &all, bloc);
			if (!ok && getenv("VCC_CXX_GEN"))
				fprintf(stderr, "no code (constructor: %s): %.*s\n", cxx_class_has_member_initializer(c, f->member_of->owner->ty) ?
					"members with default initializers" : "a base or member to default-construct",
					(int)vintern_ents[f->member_of->owner->name].len, vintern_ents[f->member_of->owner->name].s);
			for (uint32_t i = 0; ok && pre && i < pre->n; i++)
				cc_id_list_append(a, &all, pre->v[i]);
			if (ok && !c->gen_delegates && cxx_class_polymorphic(c, f->member_of->owner->ty)) {
				/* the object is of this class from here: its vptr (members initialized by calls of virtual
				   functions would want it before them) */
				uint32_t st = cxx_gen_vptr_store(c, f->member_of, bloc);

				if (st)
					cc_id_list_append(a, &all, st);
				else
					ok = false;
			}
			if (ok && all.n) {
				cc_id_list_append(a, &all, body);
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		} else {
			/* a destructor: its body, then its members and bases (a return in the body would skip them) */
			uint32_t before;

			if (cxx_class_polymorphic(c, f->member_of->owner->ty)) {
				uint32_t st = cxx_gen_vptr_store(c, f->member_of, bloc);	/* (the body sees an object of this class) */

				if (st)
					cc_id_list_append(a, &all, st);
				else
					ok = false;
			}
			cc_id_list_append(a, &all, body);
			before = all.n;
			ok = ok && cxx_gen_destructor_rest(c, &all, bloc);
			if (ok && all.n > before)
				ok = !c->gen_returns;
			if (ok && all.n > 1) {
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		}
	}
	c->gen_pre = NULL;
	free(pre);
	uint32_t psyms = c->gen_psyms, npsyms = c->gen_npsyms;

	cxx_gen_pop(c);	/* (the body around this one, if any, goes on) */
	c->stats.functions++;
	if (ok && getenv("VCC_CXX_NOGEN") && f->sym) {
		/* (a debugging aid: the functions named in the file, one mangled name a line, get no code here, so
		   that another compiler's definition is used: bisecting which of vcc's is wrong) */
		static __thread char *list;
		static __thread size_t nlist;
		const vident *v = &vintern_ents[a->syms[f->sym].name];

		if (!list) {
			FILE *lf = fopen(getenv("VCC_CXX_NOGEN"), "r");

			list = calloc(1, 1 << 24);
			if (lf) {
				nlist = fread(list + 1, 1, (1 << 24) - 2, lf);
				fclose(lf);
			}
			list[0] = '\n';
			for (size_t i = 1; i <= nlist; i++)
				if (list[i] == '\r')
					list[i] = '\n';
		}
		for (const char *q = list; (q = strstr(q, "\n")) && q[1]; q++)
			if (!strncmp(q + 1, v->s, v->len) && (q[1 + v->len] == '\n' || !q[1 + v->len])) {
				ok = false;
				break;
			}
		if (!ok)
			return;
	}
	if (!ok)
		return;
	if (f->fn_inline) {
		/* an inline function's definition is weak (a reference to one that got no code stays a plain
		   undefined symbol: the link fails instead of calling nothing) */
		attrs at;

		memset(&at, 0, sizeof at);
		at.weak = 1;
		cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
		if (f->sym1 && f->sym1 != f->sym)
			cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);	/* (named before it was known to be inline: the alias below needs its target, which stays) */
		else
			a->syms[f->sym].flags |= SF_LINKONCE;	/* (emitted only if the TU refers to it) */
		if (f->sym0) {
			cc_attribute_apply_symbol(a, &a->syms[f->sym0], &at);
			a->syms[f->sym0].flags |= SF_LINKONCE;
		}
	}
	a->syms[f->sym].body = body;
	a->syms[f->sym].flags |= SF_DEFINED;
	a->syms[f->sym].defloc = a->syms[f->sym].loc;
	AST_NODE_AT(a, body)->a = psyms;
	AST_NODE_AT(a, body)->aux = (uint16_t)npsyms;
	a->toplevel[a->ntoplevel++] = f->sym;	/* (the TU's definitions, in order: what emit_tu walks) */
	if (f->sym1 == f->sym && cxx_gen_is_special(f) && f->sym) {
		/* (an inline one with one symbol for both names: another declaration's complete-object symbol, made
		   apart and referred to, is an alias of this one) */
		char n1[2048];
		cxx_scope *home = f->member_of ? f->member_of : f->home;
		uint32_t other;

		if (cxx_mangle_function(c, f, home, f->ty, false, 1, n1, sizeof n1) && (other = cxx_gen_symbol_named(c, n1, 0)) &&
		    other != f->sym && !(a->syms[other].flags & SF_DEFINED)) {
			const sym *y = &a->syms[f->sym];
			attrs at;

			memset(&at, 0, sizeof at);
			at.alias = a->attr[y->attrs].asmname;
			at.weak = f->fn_inline;
			cc_attribute_apply_symbol(a, &a->syms[other], &at);
			a->syms[other].flags |= SF_DEFINED;
			a->toplevel[a->ntoplevel++] = other;
		}
	}
	if (f->sym1 && f->sym1 != f->sym) {
		/* a constructor or destructor: the complete-object function is the same code (no virtual bases) */
		const sym *y = &a->syms[f->sym];
		attrs at;

		memset(&at, 0, sizeof at);
		at.alias = a->attr[y->attrs].asmname;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
		a->syms[f->sym1].flags |= SF_DEFINED;
		a->toplevel[a->ntoplevel++] = f->sym1;
	}
	if (f->fn_virtual && f->member_of) {
		if (f->sym0)
			cxx_gen_deleting_destructor(c, f);
		cxx_vtable_symbol(c, f->member_of);	/* (the key function's TU defines the vtable) */
	}
	c->stats.functions_generated++;
}
