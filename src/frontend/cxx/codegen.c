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
	return c->gen && !c->gen_failed && !c->tentative && !c->template_depth && !c->unevaluated && !c->instantiating;
}

/* the body has something code generation does not build yet */
VPRIV void cxx_gen_fail_at(cxx *c, const char *file, int line)
{
	static __thread int trace = -1;

	if (!c->gen || c->tentative || c->template_depth || c->instantiating)
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

/* a type code is generated for so far: a scalar (not a reference) */
VPRIV bool cxx_gen_type_ok(cxx *c, qty t)
{
	int k;

	if (t == CXX_TY_UNKNOWN)
		return false;
	k = AST_TYPE_KIND(c->ast, t);
	return (k >= TY_BOOL && k <= TY_ULLONG) || k == TY_FLOAT || k == TY_DOUBLE || (k == TY_PTR && !AST_TYPE(c->ast, t)->ext);
}

/* ... or an array of them (a variable's type; a parameter's has decayed) */
static bool cxx_gen_object_type_ok(cxx *c, qty t)
{
	while (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY && AST_TYPE(c->ast, t)->n)
		t = AST_TYPE(c->ast, t)->base;
	return cxx_gen_type_ok(c, t);
}

/*
 * The function's symbol name: C++'s mangled name (mangle.c), or the plain name for main and extern "C".
 * False: a name not mangled yet.
 */
static bool cxx_gen_name(cxx *c, cxx_entity *f)
{
	cc *a = c->ast;
	const vident *v = &vintern_ents[f->name];
	char name[2048];
	attrs at;
	char *keep;
	size_t n;
	uint32_t si;

	if (f->mangled || f->c_linkage)
		return true;
	if (f->member_of || !f->home)
		return false;
	if (f->home == c->global && v->len == 4 && !memcmp(v->s, "main", 4)) {
		f->mangled = 1;
		return true;
	}
	if (!cxx_mangle_function(c, f, f->home, f->ty, false, name, sizeof name))
		return false;
	/* the symbol's assembler name, as an asm label gives one in C */
	n = strlen(name);
	keep = cc_heap_allocate(a, n + 1);
	memcpy(keep, name, n + 1);
	si = a->nstr++;
	a->strs[si] = (cstr){ keep, n + 1, 1, LEX_TOKEN_STRING };
	memset(&at, 0, sizeof at);
	at.asmname = si + 1;
	cc_attribute_apply_symbol(a, &a->syms[f->sym], &at);
	f->mangled = 1;
	return true;
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

	if (!cxx_gen_on(c) || !v->sym || !cxx_gen_object_type_ok(c, v->ty))
		return;
	n = cc_node_new(c->ast, N_VAR, c->ast->syms[v->sym].ty, loc);
	AST_NODE_AT(c->ast, n)->a = v->sym;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
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
	if (!cxx_gen_on(c) || !x || !cxx_gen_type_ok(c, to))
		return;
	x = cc_conversion_decay(a, x);
	if (!cxx_gen_type_ok(c, AST_NODE_AT(a, x)->ty))
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
VPRIV void cxx_gen_call(cxx *c, cxx_entity *f, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const ctype *ft;
	cc_id_list list;
	uint32_t fn, n;

	if (!cxx_gen_on(c) || !f || f->kind != CXX_ENTITY_VALUE || !f->sym || f->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(a, f->ty) != TY_FUNC ||
	    nargs > 16)
		return;
	ft = AST_TYPE(a, f->ty);
	if (ft->n != nargs || (ft->flags & TF_VARIADIC) || (!cxx_gen_type_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		return;	/* (default arguments, ...: not built yet) */
	for (uint32_t i = 0; i < nargs; i++)
		if (!args[i] || !cxx_gen_type_ok(c, a->params[ft->aux + i]))
			return;
	if (!cxx_gen_name(c, f))
		return;
	cc_id_list_init(&list);
	for (uint32_t i = 0; i < nargs; i++) {
		qty pt = a->params[ft->aux + i];

		cc_id_list_append(a, &list, cc_conversion_apply(a, cc_conversion_decay(a, args[i]), QUNQ(pt)));
	}
	fn = cc_node_new(a, N_VAR, a->syms[f->sym].ty, loc);
	AST_NODE_AT(a, fn)->a = f->sym;
	AST_NODE_AT(a, fn)->flags = NF_LVALUE;
	fn = cc_node_rvalue(a, fn);	/* (the function designator: a pointer to it) */
	n = cc_node_new(a, N_CALL, QUNQ(ft->base), loc);
	AST_NODE_AT(a, n)->a = fn;
	AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
	AST_NODE_AT(a, n)->c = nargs;
	c->enode = n;
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
	if (has_value)
		e = cc_conversion_apply(a, cc_conversion_decay(a, e), QUNQ(c->gen_ret));
	return cc_statement_new(a, N_RETURN, loc, e, 0, 0);
}

/* a local variable of the body: its symbol (0: a type not generated yet) */
VPRIV uint32_t cxx_gen_local(cxx *c, uint32_t name, qty t, uint32_t loc)
{
	cc *a = c->ast;
	uint32_t s;

	if (!cxx_gen_on(c))
		return 0;
	if (!cxx_gen_object_type_ok(c, t)) {
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
	if (has_init) {
		if (AST_NODE_AT(a, init)->op != N_INIT)
			init = cc_conversion_apply(a, cc_conversion_decay(a, init), QUNQ(a->syms[s].ty));
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

	c->gen = 0, c->gen_failed = 0;
	if (!a->o->emit || c->tentative || c->template_depth || c->instantiating || !f || !f->sym || f->ty == CXX_TY_UNKNOWN ||
	    AST_TYPE_KIND(a, f->ty) != TY_FUNC || (a->syms[f->sym].flags & SF_DEFINED))
		return false;
	ft = AST_TYPE(a, f->ty);
	if ((ft->flags & TF_VARIADIC) || ft->n != nps || (!cxx_gen_type_ok(c, ft->base) && AST_TYPE_KIND(a, ft->base) != TY_VOID))
		return false;
	for (uint32_t i = 0; i < nps; i++)
		if (!cxx_gen_type_ok(c, ps[i].ty))
			return false;
	if (!cxx_gen_name(c, f))
		return false;
	cc_scope_open(a);
	cc_id_list_init(&psyms);
	for (uint32_t i = 0; i < nps; i++) {
		uint32_t s = cc_symbol_new(a, S_VAR, ps[i].name > 1 ? ps[i].name : 0, QUNQ(ps[i].ty), a->syms[f->sym].loc);
		cxx_entity *pe = ps[i].name > 1 ? cxx_lookup_in(c, fs, ps[i].name, 0) : NULL;

		a->syms[s].flags |= SF_PARAM;
		a->syms[s].sc = SC_AUTO;
		cc_id_list_append(a, &psyms, s);
		if (pe)
			pe->sym = s;
	}
	c->gen_psyms = cc_id_list_freeze(a, &psyms), c->gen_npsyms = nps;
	c->gen_ret = ft->base;
	c->gen_fn = a->fn;
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

	c->gen = 0;
	a->fn = c->gen_fn;
	cc_scope_close(a);
	c->stats.functions++;
	if (!ok)
		return;
	a->syms[f->sym].body = body;
	a->syms[f->sym].flags |= SF_DEFINED;
	a->syms[f->sym].defloc = a->syms[f->sym].loc;
	AST_NODE_AT(a, body)->a = c->gen_psyms;
	AST_NODE_AT(a, body)->aux = (uint16_t)c->gen_npsyms;
	a->toplevel[a->ntoplevel++] = f->sym;	/* (the TU's definitions, in order: what emit_tu walks) */
	c->stats.functions_generated++;
}
