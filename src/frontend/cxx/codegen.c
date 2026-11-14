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
 * A class whose objects code is generated for: laid out, with no base class, and members that need no
 * constructor or destructor call of their own (scalars, arrays of them, classes C copies as C++ does).
 * It may declare constructors and a destructor; passing it by value is then not built yet (cxx_gen_type_ok).
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
	cls = cxx_tag_scope_get(c, QUNQ(t));
	if (!cls || cls->nused || cls->has_dependent_base)
		return false;
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		qty ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(a, ft) == TY_ARRAY)
			ft = AST_TYPE(a, ft)->base;
		if (!cxx_gen_type_ok(c, ft) || cxx_gen_is_reference(c, ft))
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
	return cc_conversion_apply(a, cc_conversion_decay(a, x), QUNQ(to));
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
	bt = AST_NODE_AT(a, base)->ty;
	if (!cc_type_is_record(a, bt) || !(a->tags[AST_TYPE(a, bt)->aux].flags & RF_COMPLETE))
		return;
	{
		uint64_t bits;

		if (!cxx_class_field_offset(c, bt, name, &bits, 0))
			return;	/* (a base class's member, a static one: not a field of this record) */
	}
	c->enode = member(a, base, name, loc);
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
	if (ps[0] == CXX_TY_UNKNOWN)
		return CXX_TY_UNKNOWN;
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
	if (!cxx_gen_on(c) || !x || !cxx_gen_type_ok(c, to) || cxx_gen_is_reference(c, to) || cc_type_is_record(a, to))
		return;
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty) || cc_type_is_record(a, AST_NODE_AT(a, x)->ty))
		return;
	c->enode = cc_cast_insert(a, x, QUNQ(to), cc_conversion_cast_kind(a, AST_NODE_AT(a, x)->ty, QUNQ(to), x));
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
		cc_id_list_append(a, &list, cc_cast_insert(a, self, tt, CK_NOP));
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

/* ---- constructors and destructors ------------------------------------------------------------ */

static uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t n = cc_node_new(c->ast, N_VAR, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, n)->a = s;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	return n;
}

/* a constructor's member initializer name(args): this->name = arg for a scalar member (zero for none) */
VPRIV void cxx_gen_member_initializer(cxx *c, uint32_t name, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t target, value;
	qty mt;

	if (!cxx_gen_on(c) || !c->gen_this || !c->gen_pre)
		return;
	cxx_gen_this(c, loc);
	cxx_gen_member(c, c->enode, true, name, loc);
	target = c->enode;
	c->enode = 0;
	if (!target || nargs > 1 || nargs == UINT32_MAX || (nargs == 1 && !args[0])) {
		cxx_gen_fail(c);	/* (a base class, a member of class type with arguments: not built yet) */
		return;
	}
	mt = AST_NODE_AT(a, target)->ty;
	if (cc_type_is_record(a, mt) || AST_TYPE_KIND(a, mt) == TY_ARRAY || !cxx_gen_type_ok(c, mt)) {
		if (!(nargs == 1 && cc_type_is_record(a, mt) && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(mt) && cxx_gen_type_ok(c, mt))) {
			cxx_gen_fail(c);
			return;
		}
		value = cc_node_rvalue(a, args[0]);	/* (a member of a class C copies: from an object of it) */
	} else
		value = nargs ? cc_conversion_apply(a, cc_conversion_decay(a, args[0]), QUNQ(mt)) :
				cc_conversion_apply(a, cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc), QUNQ(mt));
	if (!(AST_NODE_AT(a, target)->flags & NF_LVALUE)) {
		cxx_gen_fail(c);
		return;
	}
	cc_id_list_append(a, (cc_id_list *)c->gen_pre, cc_statement_new(a, N_EXPR, loc, cc_assignment_build(a, 0, target, value, loc), 0, 0));
}

/*
 * A local object of class type: its declaration, the call of the constructor cxx_ctor_use chose (or a
 * copy from an object of the class where none is declared for that), and its destructor at the end of
 * its scope (the emitter's scope cleanup, as C's __attribute__((cleanup))).
 */
VPRIV void cxx_gen_construct_local(cxx *c, uint32_t s, qty t, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	cxx_entity *ctor = c->ctor_picked, *dtor;
	bool found = c->ctor_found, none = c->ctor_none;
	cxx_scope *cls = cxx_tag_scope_get(c, QUNQ(t));
	cc_id_list *block = c->gen_block;

	if (!cxx_gen_on(c) || !s)
		return;
	if (!block || !cls || nargs == UINT32_MAX) {
		cxx_gen_fail(c);
		return;
	}
	/* the destructor, declared in the class (an implicit one does nothing for the classes built so far) */
	dtor = cxx_lookup_in(c, cls, cxx_dtor_id(), 0);
	if (dtor && dtor->member_of == cls && dtor->kind == CXX_ENTITY_VALUE) {
		attrs at;

		if (dtor->fn_deleted || !cxx_gen_name(c, dtor) || !dtor->sym1) {
			cxx_gen_fail(c);
			return;
		}
		memset(&at, 0, sizeof at);
		at.cleanup = dtor->sym1;
		cc_attribute_apply_symbol(a, &a->syms[s], &at);
	}
	cc_id_list_append(a, block, cc_node_new_operands(a, N_DECL, cc_type_basic(TY_VOID), loc, s, 0));
	if (found && ctor) {
		cxx_gen_call(c, ctor, cxx_gen_var_node(c, s, loc), false, args, nargs, loc);
		if (!c->enode) {
			cxx_gen_fail(c);
			return;
		}
		cc_id_list_append(a, block, cc_statement_new(a, N_EXPR, loc, c->enode, 0, 0));
		c->enode = 0;
		return;
	}
	if (nargs == 0 && none)
		return;	/* (no constructor declared: default-initialization does nothing here) */
	if (nargs == 1 && args[0] && QUNQ(AST_NODE_AT(a, args[0])->ty) == QUNQ(t) && cxx_class_special(c, QUNQ(t), 0, false, 0) == 1 && !found) {
		/* from an object of the class, with no copy constructor declared: copied member by member, as C copies */
		cxx_entity *copy = NULL;

		for (cxx_entity *e = cxx_lookup_in(c, cls, cxx_ctor_id(), 0); e; e = e->next)
			if (e->member_of == cls && e->kind == CXX_ENTITY_VALUE && e->ty != CXX_TY_UNKNOWN && AST_TYPE(a, e->ty)->n == 1) {
				qty p = a->params[AST_TYPE(a, e->ty)->aux];

				if (cxx_gen_is_reference(c, p) && QUNQ(AST_TYPE(a, p)->base) == QUNQ(t))
					copy = e;
			}
		if (!copy) {
			cc_id_list_append(a, block, cc_statement_new(a, N_EXPR, loc,
								     cc_assignment_build(a, 0, cxx_gen_var_node(c, s, loc), cc_node_rvalue(a, args[0]), loc), 0, 0));
			return;
		}
	}
	cxx_gen_fail(c);	/* (an aggregate from a list, a constructor not resolved: not built yet) */
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

	c->gen_pre = NULL;
	if (ok && pre && pre->n) {
		/* a constructor: its member initializers, then its body */
		cc_id_list_append(a, pre, body);
		body = cc_statement_new(a, N_BLOCK, AST_NODE_AT(a, body)->loc, 0, cc_id_list_freeze(a, pre), pre->n);
	}
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
