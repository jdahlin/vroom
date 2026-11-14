/*
 * typing.c: the types of C++ expressions (PLAN-cxx.md, the semantic
 * analysis the 30x estimate depends on), first form.
 *
 * Each expression parse function leaves its result in c->etype (an AST type,
 * CXX_TY_UNKNOWN when not known) and c->elval (an lvalue).  A name that is a
 * set of functions leaves the set in c->ecallee for the call that follows.
 * The rules are the language's: C's for arithmetic (cc_type_arithmetic_
 * conversion, cc_type_promote), a reference's referent as an lvalue, the
 * return type of the function a call selects.  Overload resolution is its
 * first form: by the number of arguments, then by exact argument types;
 * an ambiguity is unknown, never a guess.
 */
#include "cxx-internal.h"

VPRIV void cxx_et(cxx *c, qty t, bool lvalue)
{
	c->ecallee = NULL;
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext) {
		/* a reference: its referent, an lvalue (T&& names are lvalues too) */
		c->etype = AST_TYPE(c->ast, t)->base, c->elval = 1;
		return;
	}
	c->etype = t, c->elval = lvalue;
}

/* an integer or floating literal's type: C's rules (the suffix, the form) */
VPRIV qty cxx_number_type(cxx *c, const cxx_token *t)
{
	bool fp = false, hex = t->len > 1 && t->s[0] == '0' && (t->s[1] == 'x' || t->s[1] == 'X');
	int longs = 0, uns = 0, f = 0;

	for (uint32_t i = 0; i < t->len; i++) {
		char ch = t->s[i];

		if (ch == '.' || (!hex && (ch == 'e' || ch == 'E')) || (hex && (ch == 'p' || ch == 'P')))
			fp = true;
	}
	for (uint32_t i = t->len; i-- > 0;) {
		char ch = t->s[i];

		if (ch == 'l' || ch == 'L')
			longs++;
		else if (ch == 'u' || ch == 'U')
			uns = 1;
		else if (fp && (ch == 'f' || ch == 'F'))
			f = 1;
		else
			break;
	}
	if (fp)
		return cc_type_basic(f ? TY_FLOAT : longs ? TY_LDOUBLE : TY_DOUBLE);
	if (longs >= 2)
		return cc_type_basic(uns ? TY_ULLONG : TY_LLONG);
	if (longs)
		return cc_type_basic(uns ? TY_ULONG : TY_LONG);
	/* (an unsuffixed literal too big for int is long: not checked here) */
	return cc_type_basic(uns ? TY_UINT : TY_INT);
}

static bool cxx_is_arith(cxx *c, qty t)
{
	return t != CXX_TY_UNKNOWN && cc_type_is_arithmetic(c->ast, t);
}

static bool cxx_is_ptr(cxx *c, qty t)
{
	return t != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, t) == TY_PTR || AST_TYPE_KIND(c->ast, t) == TY_ARRAY);
}

static qty cxx_decay(cxx *c, qty t)
{
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_ARRAY)
		return cc_type_pointer(c->ast, AST_TYPE(c->ast, t)->base);
	return t;
}

/* a binary operator's result (kind: the operator), from the operands' types */
VPRIV void cxx_binary_type(cxx *c, int op, qty l, qty r)
{
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL:
	case LEX_TOKEN_GREATER: case LEX_TOKEN_GREATER_EQUAL: case LEX_TOKEN_AMPERSAND_AMPERSAND: case LEX_TOKEN_PIPE_PIPE:
		/* built in on arithmetic and pointers: bool (a class's operator: not resolved yet) */
		cxx_et(c, (cxx_is_arith(c, l) || cxx_is_ptr(c, l)) && (cxx_is_arith(c, r) || cxx_is_ptr(c, r)) ?
			  cc_type_basic(TY_BOOL) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER:
		cxx_et(c, cxx_is_arith(c, l) && cxx_is_arith(c, r) ? cc_type_promote(c->ast, QUNQ(l)) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS:
		if (cxx_is_ptr(c, l) && cxx_is_arith(c, r)) {
			cxx_et(c, QUNQ(cxx_decay(c, l)), false);
			return;
		}
		if (op == LEX_TOKEN_PLUS && cxx_is_arith(c, l) && cxx_is_ptr(c, r)) {
			cxx_et(c, QUNQ(cxx_decay(c, r)), false);
			return;
		}
		if (op == LEX_TOKEN_MINUS && cxx_is_ptr(c, l) && cxx_is_ptr(c, r)) {
			cxx_et(c, cc_type_basic(TY_LONG), false);	/* ptrdiff_t */
			return;
		}
		__attribute__((fallthrough));
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_CARET:
	case LEX_TOKEN_PIPE:
		cxx_et(c, cxx_is_arith(c, l) && cxx_is_arith(c, r) ? cc_type_arithmetic_conversion(c->ast, QUNQ(l), QUNQ(r)) : CXX_TY_UNKNOWN,
		       false);
		return;
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
}

/* a unary operator's result */
VPRIV void cxx_unary_type(cxx *c, int op, qty t, bool lvalue)
{
	switch (op) {
	case LEX_TOKEN_AMPERSAND:
		cxx_et(c, lvalue && t != CXX_TY_UNKNOWN && !cc_type_is_record(c->ast, t) ? cxx_type_pointer(c, t, 0) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_STAR:
		cxx_et(c, cxx_is_ptr(c, t) ? AST_TYPE(c->ast, t)->base : CXX_TY_UNKNOWN, true);
		return;
	case LEX_TOKEN_EXCLAMATION:
		cxx_et(c, cxx_is_arith(c, t) || cxx_is_ptr(c, t) ? cc_type_basic(TY_BOOL) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_MINUS: case LEX_TOKEN_PLUS: case LEX_TOKEN_TILDE:
		cxx_et(c, cxx_is_arith(c, t) ? cc_type_promote(c->ast, QUNQ(t)) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_PLUS_PLUS: case LEX_TOKEN_MINUS_MINUS:
		cxx_et(c, cxx_is_arith(c, t) || cxx_is_ptr(c, t) ? t : CXX_TY_UNKNOWN, true);
		return;
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
}

/*
 * A call of the function set `set` (an entity chain: the functions among its entries) with the
 * argument types args: the return type of the one function it selects, else unknown.
 */
/* does a non-template's parameter list take exactly these argument types (references seen through)? */
static bool cxx_exact(cxx *c, const ctype *ft, const qty *args, uint32_t nargs)
{
	for (uint32_t i = 0; i < nargs && i < ft->n; i++) {
		qty p = c->ast->params[ft->aux + i];

		if (AST_TYPE_KIND(c->ast, p) == TY_PTR && AST_TYPE(c->ast, p)->ext)
			p = AST_TYPE(c->ast, p)->base;
		if (args[i] == CXX_TY_UNKNOWN || QUNQ(p) != QUNQ(args[i]))
			return false;
	}
	return true;
}

/*
 * A call's type.  set: the functions the callee names (NULL: callee_type, a function or a pointer
 * to one).  The candidates are the non-templates whose arity fits and the templates whose
 * arguments deduce; the call's type is known when one exact non-template matches, or when every
 * viable candidate returns the same type (a simplification of overload resolution).
 */
VPRIV void cxx_call_type(cxx *c, cxx_entity *set, qty callee_type, const qty *args, const uint8_t *lval, uint32_t nargs)
{
	static const uint8_t none[16];
	cxx_entity *exact = NULL;
	uint32_t viable = 0, nexact = 0;
	qty result = CXX_TY_UNKNOWN;
	bool same = true, unknown = false;

	if (!set) {
		/* a pointer to a function, a function's type */
		qty f = callee_type;

		if (f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == TY_PTR && !AST_TYPE(c->ast, f)->ext)
			f = AST_TYPE(c->ast, f)->base;
		cxx_et(c, f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == TY_FUNC ? AST_TYPE(c->ast, f)->base : CXX_TY_UNKNOWN, false);
		return;
	}
	if (!lval)
		lval = none;
	qty acopy[16];
	uint8_t lcopy[16];

	if (nargs <= 16) {	/* (instantiating a body reuses c->cargs) */
		memcpy(acopy, args, nargs * sizeof *args), memcpy(lcopy, lval, nargs);
		args = acopy, lval = lcopy;
	}
	for (cxx_entity *e = set; e; e = e->next) {
		qty r;

		if (e->kind == CXX_ENTITY_DEPENDENT) {
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
			int d = nargs <= 16 ? cxx_ftemplate_deduce(c, e, args, lval, nargs, &r) : -1;

			if (d < 0)
				unknown = true;	/* (a template vcc cannot deduce might be the best) */
			if (d <= 0)
				continue;
		} else {
			const ctype *ft;

			if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != TY_FUNC)
				continue;
			ft = AST_TYPE(c->ast, e->ty);
			if (!((nargs <= ft->n && nargs + e->ndefaults >= ft->n) || ((ft->flags & TF_VARIADIC) && nargs >= ft->n)))
				continue;
			r = ft->base;
			if (cxx_exact(c, ft, args, nargs))
				nexact++, exact = e;
		}
		if (!viable++)
			result = r;
		else if (r != result)
			same = false;
	}
	if (nexact == 1)
		result = AST_TYPE(c->ast, exact->ty)->base;	/* (an exact non-template wins over a template's equal match) */
	else if (!viable || !same || unknown)
		result = CXX_TY_UNKNOWN;
	cxx_et(c, result, false);
}

/*
 * An operator with a class operand: obj's member operator (args: the other operands), else
 * a namespace-scope one found by ordinary lookup (args: all operands; ADL not yet).  false if
 * neither is found: the built-in operator applies.
 */
VPRIV bool cxx_operator_call_type(cxx *c, uint32_t id, qty obj, const qty *args, uint32_t nargs)
{
	cxx_scope *cls;
	cxx_entity *set;
	qty all[4];

	if (id <= 1 || obj == CXX_TY_UNKNOWN || nargs > 2)
		return false;
	cls = cxx_type_class_scope(c, QUNQ(obj));
	if (!cls)
		return false;
	set = cxx_lookup_in(c, cls, id, 0);
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		cxx_call_type(c, set, CXX_TY_UNKNOWN, args, NULL, nargs);
		return true;
	}
	set = cxx_lookup(c, id);
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		all[0] = obj;
		for (uint32_t i = 0; i < nargs; i++)
			all[i + 1] = args[i];
		cxx_call_type(c, set, CXX_TY_UNKNOWN, all, NULL, nargs + 1);
		return true;
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
	return true;
}
