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
typedef struct cxx_gen_saved {
	struct cxx_gen_saved *next;
	uint8_t gen, gen_failed;
	uint32_t gen_level, enode, snode, sdone, sdepth, gen_psyms, gen_npsyms, gen_fn, gen_this, ast_fn, ast_depth;
	void *gen_block, *gen_pre;
	qty gen_ret;
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
	c->gen_stack = s;
	c->gen_stack_depth++;
}

static void cxx_gen_pop(cxx *c)
{
	cxx_gen_saved *s = c->gen_stack;

	c->gen_stack = s->next;
	c->gen_stack_depth--;
	c->gen = s->gen, c->gen_failed = s->gen_failed, c->gen_level = s->gen_level;
	c->enode = s->enode, c->snode = s->snode, c->sdone = s->sdone, c->sdepth = s->sdepth;
	c->gen_psyms = s->gen_psyms, c->gen_npsyms = s->gen_npsyms, c->gen_fn = s->gen_fn, c->gen_this = s->gen_this;
	c->gen_block = s->gen_block, c->gen_ret = s->gen_ret;
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

	if (!c->gen || c->tentative || c->template_depth || c->instantiating != c->gen_level)
		return;
	if (trace < 0)
		trace = getenv("VCC_CXX_GEN") != NULL;
	if (trace && !c->gen_failed) {	/* VCC_CXX_GEN=1: the first thing in each function that is not built, and where vcc gave up */
		fprintf(stderr, "no code (%s:%d):", strrchr(file, '/') ? strrchr(file, '/') + 1 : file, line);
		for (uint32_t i = c->pos > 10 ? c->pos - 10 : 0; i < c->pos + 3 && i < c->ntok; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	}
	c->gen_failed = 1;
}

static bool cxx_gen_object_type_ok(cxx *c, qty t);
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
	if (k == TY_PTR && AST_TYPE(c->ast, t)->ext)
		return cxx_gen_object_type_ok(c, AST_TYPE(c->ast, t)->base);
	if (k == TY_STRUCT || k == TY_UNION)
		return (c->ast->tags[AST_TYPE(c->ast, t)->aux].flags & RF_COMPLETE) && cxx_class_layout_info(c, t, &pod, &empty) && pod;
	return (k >= TY_BOOL && k <= TY_ULLONG) || k == TY_FLOAT || k == TY_DOUBLE || k == TY_PTR || k == TY_ENUM;
}

/*
 * A class whose objects code is generated for: laid out, its bases and its members of class type such
 * classes too, its other members scalars or arrays of them.  It may declare constructors and a
 * destructor; passing it by value is then not built yet (cxx_gen_type_ok).
 */
VPRIV bool cxx_gen_class_ok(cxx *c, qty t)
{
	cc *a = c->ast;
	cxx_scope *cls;
	bool pod, empty;

	if (t == CXX_TY_UNKNOWN || (AST_TYPE_KIND(a, t) != TY_STRUCT && AST_TYPE_KIND(a, t) != TY_UNION) ||
	    !(a->tags[AST_TYPE(a, t)->aux].flags & RF_COMPLETE) || !cxx_class_layout_info(c, t, &pod, &empty))
		return false;
	if (pod)
		return true;
	if (cxx_class_polymorphic(c, t))
		return false;	/* (vtables: not built yet) */
	cls = cxx_tag_scope_get(c, QUNQ(t));
	if (!cls || cls->has_dependent_base)
		return false;
	{
		qty bases[16];
		uint32_t nb = cxx_class_bases(c, t, bases, 16);

		for (uint32_t i = 0; i < nb; i++)
			if (!cxx_gen_class_ok(c, bases[i]))
				return false;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (ft != CXX_TY_UNKNOWN && cc_type_is_record(a, ft)) {
			if (!cxx_gen_class_ok(c, ft))
				return false;
		} else if (!cxx_gen_type_ok(c, ft) || cxx_gen_is_reference(c, ft))
			return false;
	}
	return true;
}

/* ... or an array of them (a variable's type; a parameter's has decayed), or such a class */
static bool cxx_gen_object_type_ok(cxx *c, qty t)
{
	while (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY && AST_TYPE(c->ast, t)->n)
		t = AST_TYPE(c->ast, t)->base;
	if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != TY_ARRAY)
		return cxx_gen_class_ok(c, t);
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

/*
 * The value x for an object, parameter or result of type to: converted for a scalar, as it is for a class,
 * and for a reference the address of the lvalue it binds to (for a const T& or T&& bound to a value: of a
 * temporary holding it).  0: not built.
 */
static uint32_t cxx_gen_bind(cxx *c, uint32_t x, qty to, uint32_t loc)
{
	cc *a = c->ast;

	if (!x)
		return 0;
	if (cxx_gen_is_reference(c, to)) {
		qty rt = AST_TYPE(a, to)->base;

		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && cc_type_is_record(a, rt) && cc_type_is_record(a, AST_NODE_AT(a, x)->ty) &&
		    QUNQ(AST_NODE_AT(a, x)->ty) != QUNQ(rt)) {
			/* a reference to a base class bound to a derived object: its base subobject */
			x = cxx_gen_base_object(c, x, QUNQ(rt), loc);
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
	if (cc_type_is_record(a, to))
		return QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(to) ? cc_node_rvalue(a, x) : 0;
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
	if (c->enode && cxx_gen_is_reference(c, AST_NODE_AT(a, c->enode)->ty))
		c->enode = cxx_gen_referent(c, c->enode, loc);
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

/* the function's type as the ABI calls it: this first for a member function */
static qty cxx_gen_abi_type(cxx *c, const cxx_entity *f)
{
	cc *a = c->ast;
	const ctype *ft = AST_TYPE(a, f->ty);
	qty ps[33];
	uint32_t np = 0;

	if (!cxx_gen_has_this(f))
		return f->ty;
	if (ft->n > 31)
		return CXX_TY_UNKNOWN;
	ps[np++] = cxx_gen_this_type(c, f);
	if (ps[0] == CXX_TY_UNKNOWN || cxx_class_polymorphic(c, AST_TYPE(a, ps[0])->base))
		return CXX_TY_UNKNOWN;	/* (a class with virtual functions: vtables are not built yet) */
	for (uint32_t i = 0; i < ft->n; i++)
		ps[np++] = a->params[ft->aux + i];
	ft = AST_TYPE(a, f->ty);
	return cxx_type_function(c, ft->base, ps, np, (ft->flags & TF_VARIADIC) != 0);
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
	if (!cxx_mangle_function(c, f, home, f->ty, f->member_of && f->fn_const, special ? 2 : 0, name, sizeof name))
		return false;
	if (!f->sym) {
		qty st = cxx_gen_abi_type(c, f);

		if (st == CXX_TY_UNKNOWN)
			return false;
		f->sym = cc_symbol_new(a, S_FUNC, f->name, st, 0);
		if (!f->fn_internal || f->member_of)
			a->syms[f->sym].flags |= SF_GLOBAL;
		a->syms[f->sym].depth = 0;	/* (made while a body is being built: a file-scope symbol all the same) */
	}
	memset(&at, 0, sizeof at);
	at.asmname = cxx_gen_text(c, name) + 1;
	at.weak = f->fn_inline;
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	if (special) {
		if (!cxx_mangle_function(c, f, home, f->ty, false, 1, name, sizeof name))
			return false;
		f->sym1 = cc_symbol_new(a, S_FUNC, f->name, a->syms[f->sym].ty, 0);
		a->syms[f->sym1].flags |= SF_GLOBAL;
		a->syms[f->sym1].depth = 0;
		memset(&at, 0, sizeof at);
		at.asmname = cxx_gen_text(c, name) + 1;
		at.weak = f->fn_inline;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
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

	if (!cxx_gen_on(c) || (k != TY_FLOAT && k != TY_DOUBLE) || len >= sizeof buf)
		return;
	for (size_t i = 0; i < len; i++)
		if (text[i] != '\'')
			buf[n++] = text[i];
	buf[n] = 0;
	c->enode = cc_node_float_new(c->ast, strtod(buf, NULL), t, loc);	/* (strtod stops at the suffix) */
}

/* a variable (a parameter, a local, one at namespace scope) by name */
VPRIV void cxx_gen_variable(cxx *c, const cxx_entity *v, uint32_t loc)
{
	uint32_t n;

	if (!cxx_gen_on(c) || !v->sym || !(cxx_gen_object_type_ok(c, v->ty) || cxx_gen_type_ok(c, v->ty)))
		return;
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[v->sym].ty, loc);
	AST_NODE_AT(c->ast, n)->a = v->sym;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	if (cxx_gen_is_reference(c, c->ast->syms[v->sym].ty))
		n = cxx_gen_referent(c, n, loc);	/* (a reference names what it refers to) */
	c->enode = n;
}

/* op x with the built-in operator: + - ~ ! * & ++ -- (prefix) */
VPRIV void cxx_gen_unary(cxx *c, int op, uint32_t x, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t r;

	if (!cxx_gen_on(c) || !x)
		return;
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
		/* static_cast<T&&>(x), static_cast<T&>(x) of an lvalue of that type: the same object */
		if ((AST_NODE_AT(a, x)->flags & NF_LVALUE) && QUNQ(AST_NODE_AT(a, x)->ty) == QUNQ(AST_TYPE(a, to)->base))
			c->enode = x;
		return;
	}
	if (!cxx_gen_type_ok(c, to) || cc_type_is_record(a, to))
		return;
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty) || cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, AST_NODE_AT(a, x)->ty, QUNQ(to), x));
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

	if (!cxx_gen_on(c) || !cond || !x || !y)
		return;
	cond = cc_condition_scalar(a, cc_conversion_decay(a, cond), "'?:'");
	c->enode = cc_conditional_build(a, cond, x, y, loc);
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
VPRIV void cxx_gen_binary(cxx *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	if (!cxx_gen_on(c) || !l || !r)
		return;
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
	c->enode = cc_assignment_build(c->ast, base, l, r, loc);
}

/* f(args): the function overload resolution picked, the arguments converted to its parameters' types */
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, uint32_t object, bool arrow, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list list;
	uint32_t fn, n, self = 0;

	if (!cxx_gen_on(c) || !f || f->kind != CXX_ENTITY_VALUE || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC || nargs > 16)
		return;
	ft = AST_TYPE(a, f->ty);
	if (ft->n != nargs || (ft->flags & TF_VARIADIC) || (!cxx_gen_type_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		return;	/* (default arguments, ...: not built yet) */
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i] || !cxx_gen_type_ok(c, a->params[ft->aux + i]))
			return;
	if (!cxx_gen_on(c) || !cxx_gen_name(c, f) || !f->sym)
		return;
	ft = AST_TYPE(a, f->ty);
	cc_id_list_init(&list);
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
			return;	/* (a member called on a value: a temporary, not built yet) */
		self = cxx_gen_base_pointer(c, self, AST_TYPE(a, tt)->base, loc);	/* (a base class's member: on the base subobject) */
		if (!self)
			return;
		cc_id_list_append(a, &list, self);
	}
	for (uint32_t i = 0; i < nargs; i++) {
		uint32_t arg = cxx_gen_bind(c, args[i], a->params[ft->aux + i], loc);

		if (!arg)
			return;
		cc_id_list_append(a, &list, arg);
	}
	{
		uint32_t target = f->sym1 ? f->sym1 : f->sym;	/* (a constructor or destructor: the complete object's) */

		fn = cc_node_new(a, N_VAR, a->syms[target].ty, loc);
		AST_NODE_AT(a, fn)->a = target;
		AST_NODE_AT(a, fn)->flags = NF_LVALUE;
		fn = cc_node_rvalue(a, fn);	/* (the function designator: a pointer to it) */
	}
	n = cc_node_new(a, N_CALL, QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = list.n;
	if (cxx_gen_is_reference(c, ft->base))
		n = cxx_gen_referent(c, n, loc);
	c->enode = n;
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
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19) && nargs == 1 && arg0 != CXX_TY_UNKNOWN) {
		*ret = cc_type_pointer(c->ast, arg0);
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
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19)) {
		if (args[0] && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE))
			c->enode = cc_address_of_build(a, args[0], loc);
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
	if (!cls || depth > 8)
		return true;
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
static bool cxx_gen_construct_at(cxx *c, uint32_t target, qty t, const uint32_t *args, uint32_t nargs, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *ctor = c->ctor_picked;
	bool found = c->ctor_found;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));

	if (!target || !cls || nargs == UINT32_MAX || nargs > 16)
		return false;
	if (found && ctor) {
		cxx_gen_call(c, ctor, target, false, args, nargs, loc);
		if (!c->enode)
			return false;
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		return true;
	}
	if (nargs == 0)
		return !cxx_gen_needs_construction(c, t, 0);	/* (an implicit default constructor that does something: not built yet) */
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) && cxx_gen_type_ok(c, t)) {
		/* from an object of a class C copies as C++ does */
		cc_id_list_append(a, out, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, cc_node_rvalue(a, args[0]), loc), 0, 0));
		return true;
	}
	return false;	/* (an aggregate from a list, an implicit copy of a class with constructors: not built yet) */
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
			target = cxx_gen_base_object(c, self, QUNQ(class_type), loc);
		if (!target || !cxx_gen_construct_at(c, target, QUNQ(class_type), args, nargs, c->gen_pre, loc)) {
			cxx_gen_fail(c);
			return;
		}
		if (c->ngen_init_bases < 16)
			c->gen_init_bases[c->ngen_init_bases++] = QUNQ(class_type);
		return;
	}
	{
		/* (a reference member: bound, not assigned; not built yet) */
		const tag *r = &a->tags[AST_TYPE(a, AST_NODE_AT(a, self)->ty)->aux];
		bool found = false;

		for (uint32_t i = 0; i < r->nfield; i++)
			if (r->f[i].name == name) {
				found = true;
				if (cxx_gen_is_reference(c, r->f[i].ty)) {
					cxx_gen_fail(c);
					return;
				}
			}
		if (!found) {
			cxx_gen_fail(c);	/* (a member of an anonymous union) */
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
static bool cxx_gen_constructor_defaults(cxx *c, cc_id_list *first, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t self = cxx_gen_this_object(c, loc);
	qty own, bases[16];
	uint32_t nb;
	const tag *r;

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
		if (!cxx_gen_construct_at(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], NULL, 0, first, loc))
			return false;
	}
	r = &a->tags[AST_TYPE(a, own)->aux];
	for (uint32_t i = 0; i < r->nfield; i++) {
		qty ft = r->f[i].ty;
		bool named = false;

		if (!r->f[i].name || !cc_type_is_record(a, ft) || AST_TYPE_KIND(a, ft) == TY_ARRAY) {
			/* (an array of a class with a constructor, an anonymous member holding one: not built yet) */
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

/* ~T(): the destructor's call on the lvalue x (the class declares it); the statement goes to `out` */
static bool cxx_gen_destroy(cxx *c, uint32_t x, qty t, cc_id_list *out, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *d = cxx_gen_declared_destructor(c, t);

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
		if (!cxx_gen_destroy(c, cxx_gen_base_object(c, cxx_gen_this_object(c, loc), bases[i], loc), bases[i], after, loc))
			return false;
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
	cxx_entity *picked = c->ctor_picked;
	bool found = c->ctor_found, none = c->ctor_none;

	if (!cxx_gen_on(c) || !s)
		return;
	if (!block || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	dtor = cxx_gen_declared_destructor(c, t);
	if (dtor) {
		attrs at;

		if (dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1) {
			cxx_gen_fail(c);
			return;
		}
		memset(&at, 0, sizeof at);
		at.cleanup = dtor->sym1;
		cc_attribute_apply_symbol(a, &a->syms[s], &at);
	} else if (cxx_gen_needs_destruction(c, t, 0)) {
		cxx_gen_fail(c);	/* (an implicit destructor that destroys members: not built yet) */
		return;
	}
	cc_id_list_append(a, block, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, 0));
	c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none;
	if (!cxx_gen_construct_at(c, cxx_gen_var_node(c, s, loc), t, args, nargs, block, loc))
		cxx_gen_fail(c);
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

/* a condition: the expression's node as a scalar truth value (0: not built) */
VPRIV uint32_t cxx_gen_condition(cxx *c, uint32_t e)
{
	if (!cxx_gen_on(c))
		return 0;
	if (!e) {
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
	if (has_value && (!e || AST_TYPE_KIND(a, c->gen_ret) == TY_VOID)) {
		cxx_gen_fail(c);
		return 0;
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
	if (has_init && AST_NODE_AT(a, init)->op != N_INIT && !cxx_gen_type_ok(c, a->syms[s].ty)) {
		cxx_gen_fail(c);	/* (an array from something that is not a list) */
		return;
	}
	if (!has_init && cxx_gen_is_reference(c, a->syms[s].ty)) {
		cxx_gen_fail(c);
		return;
	}
	if (has_init) {
		if (AST_NODE_AT(a, init)->op != N_INIT)
			init = cxx_gen_bind(c, init, a->syms[s].ty, loc);
		if (!init) {
			cxx_gen_fail(c);
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

	if (!a->o->emit || c->tentative || c->template_depth || c->instantiating || c->gen || !v || !v->sym || !v->global_ns ||
	    !cxx_gen_object_type_ok(c, v->ty))
		return false;
	c->gen = 1, c->gen_failed = 0, c->gen_block = NULL;
	return true;
}

VPRIV void cxx_gen_global_end(cxx *c, const cxx_entity *v, uint32_t init, bool has_init)
{
	cc *a = c->ast;
	sym *y = &a->syms[v->sym];
	bool failed = c->gen_failed;

	c->gen = 0;
	if (y->flags & (SF_DEFINED | SF_TENTATIVE))
		return;
	if (!has_init) {
		y->flags |= SF_TENTATIVE;
		a->toplevel[a->ntoplevel++] = v->sym;
		return;
	}
	if (failed || !init)
		return;	/* (an initializer not built yet: the variable stays a declaration) */
	if (AST_NODE_AT(a, init)->op != N_INIT) {
		if (!cxx_gen_type_ok(c, v->ty))
			return;
		init = cc_conversion_apply(a, cc_conversion_decay(a, init), QUNQ(y->ty));
	}
	if (!cc_initializer_static_is_ok(a, y->ty, init, NULL))
		return;
	y->body = init;
	y->flags |= SF_DEFINED;
	a->toplevel[a->ntoplevel++] = v->sym;
}

/* ---- functions ----------------------------------------------------------------------------- */

/*
 * A function definition at namespace scope, before its body: its parameters' symbols, bound to the
 * parameter entities of the body's scope fs.  False: not a function code is generated for yet.
 */
VPRIV bool cxx_gen_function_begin(cxx *c, cxx_entity *f, cxx_scope *fs, const cxx_param *ps, uint32_t nps)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list psyms;

	if (!a->o->emit || c->tentative || c->template_depth || c->unevaluated || !f || f->ty == CXX_TY_UNKNOWN ||
	    AST_TYPE_KIND(a, f->ty) != TY_FUNC || (f->sym && (a->syms[f->sym].flags & SF_DEFINED)) || c->gen_stack_depth > 48)
		return false;
	ft = AST_TYPE(a, f->ty);
	if ((ft->flags & TF_VARIADIC) || ft->n != nps || (!cxx_gen_type_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		return false;
	for (uint32_t i = 0; i < nps; i++)
		if (!cxx_gen_type_ok(c, ps[i].ty))
			return false;
	if (!cxx_gen_name(c, f) || !f->sym)
		return false;
	{
		/* Two definitions with one symbol name: char16_t and unsigned short (and wchar_t, char32_t,
		   char8_t) are one type here for now, so overloads on them mangle alike.  The first has the code. */
		const sym *y = &a->syms[f->sym];
		const char *nm = y->attrs && a->attr[y->attrs].asmname ? a->strs[a->attr[y->attrs].asmname - 1].s : NULL;

		for (uint32_t i = 0; nm && i < c->ngen_names; i++)
			if (!strcmp(c->gen_names[i], nm))
				return false;
		if (nm) {
			if (c->ngen_names == c->cgen_names)
				c->gen_names = realloc(c->gen_names, (c->cgen_names = c->cgen_names ? c->cgen_names * 2 : 256) * sizeof *c->gen_names);
			c->gen_names[c->ngen_names++] = (char *)nm;
		}
	}
	ft = AST_TYPE(a, f->ty);
	/* (from here the body is being built: what was, if anything, is set aside until cxx_gen_function_end) */
	cxx_gen_push(c);
	c->gen = 0, c->gen_failed = 0, c->gen_this = 0, c->gen_pre = NULL, c->gen_level = c->instantiating;
	cc_scope_open(a);
	cc_id_list_init(&psyms);
	if (cxx_gen_has_this(f)) {
		uint32_t s = cc_symbol_new(a, S_VAR, vintern_cstr("this"), cxx_gen_this_type(c, f), a->syms[f->sym].loc);

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		c->gen_this = s;
	}
	for (uint32_t i = 0; i < nps; i++) {
		uint32_t s = cc_symbol_new(a, S_VAR, ps[i].name > 1 ? ps[i].name : 0, QUNQ(ps[i].ty), a->syms[f->sym].loc);
		cxx_entity *pe = ps[i].name > 1 ? cxx_lookup_in(c, fs, ps[i].name, 0) : NULL;

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		if (pe)
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
	return true;
}

/* ... after its body (body: the compound statement's node): defined, for emit_tu, if all of it was built */
VPRIV void cxx_gen_function_end(cxx *c, cxx_entity *f, uint32_t body)
{
	cc *a = c->ast;
	bool ok = c->gen && !c->gen_failed && body;
	cc_id_list *pre = c->gen_pre;

	if (ok && cxx_gen_is_special(f) && c->gen_this) {
		const vident *fv = &vintern_ents[f->name];
		uint32_t bloc = AST_NODE_AT(a, body)->loc;
		cc_id_list all;

		cc_id_list_init(&all);
		if (fv->s[1] == 'c') {
			/* a constructor: the bases and members its initializers did not name, the initializers, its body */
			ok = cxx_gen_constructor_defaults(c, &all, bloc);
			for (uint32_t i = 0; ok && pre && i < pre->n; i++)
				cc_id_list_append(a, &all, pre->v[i]);
			if (ok && all.n) {
				cc_id_list_append(a, &all, body);
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		} else {
			/* a destructor: its body, then its members and bases (a return in the body would skip them) */
			cc_id_list_append(a, &all, body);
			ok = cxx_gen_destructor_rest(c, &all, bloc);
			if (ok && all.n > 1) {
				ok = !c->gen_returns;
				body = cc_statement_new(a, N_BLOCK, bloc, 0, cc_id_list_freeze(a, &all), all.n);
			}
		}
	}
	c->gen_pre = NULL;
	free(pre);
	uint32_t psyms = c->gen_psyms, npsyms = c->gen_npsyms;

	cxx_gen_pop(c);	/* (the body around this one, if any, goes on) */
	c->stats.functions++;
	if (!ok)
		return;
	a->syms[f->sym].body = body;
	a->syms[f->sym].flags |= SF_DEFINED;
	a->syms[f->sym].defloc = a->syms[f->sym].loc;
	AST_NODE_AT(a, body)->a = psyms;
	AST_NODE_AT(a, body)->aux = (uint16_t)npsyms;
	a->toplevel[a->ntoplevel++] = f->sym;	/* (the TU's definitions, in order: what emit_tu walks) */
	if (f->sym1) {
		/* a constructor or destructor: the complete-object function is the same code (no virtual bases) */
		const sym *y = &a->syms[f->sym];
		attrs at;

		memset(&at, 0, sizeof at);
		at.alias = a->attr[y->attrs].asmname;
		cc_attribute_apply_symbol(a, &a->syms[f->sym1], &at);
		a->syms[f->sym1].flags |= SF_DEFINED;
		a->toplevel[a->ntoplevel++] = f->sym1;
	}
	c->stats.functions_generated++;
}
