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
	c->ecallee = NULL, c->xargs_set = 0, c->call_obj = 0, c->econst = 0;
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
static void cxx_call_type_impl(cxx *c, cxx_entity *set, qty callee_type, const cxx_targ *xargs, uint32_t nxargs, const qty *args,
			       const uint8_t *lval, uint32_t nargs, bool operator_call);

VPRIV void cxx_call_type(cxx *c, cxx_entity *set, qty callee_type, const qty *args, const uint8_t *lval, uint32_t nargs)
{
	cxx_call_type_impl(c, set, callee_type, NULL, UINT32_MAX, args, lval, nargs, false);
}

/* f<xargs>(args): only templates are candidates, their first parameters given */
VPRIV void cxx_call_type_x(cxx *c, cxx_entity *set, const cxx_targ *xargs, uint32_t nxargs, const qty *args, const uint8_t *lval,
			   uint32_t nargs)
{
	cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, xargs, nxargs, args, lval, nargs, false);
}

static void cxx_call_type_impl(cxx *c, cxx_entity *set, qty callee_type, const cxx_targ *xargs, uint32_t nxargs, const qty *args,
			       const uint8_t *lval, uint32_t nargs, bool operator_call)
{
	static const uint8_t none[16];
	cxx_entity *exact = NULL, *first = NULL;
	uint32_t viable = 0, nexact = 0;
	uint8_t obj = c->call_obj;	/* a member call's object: const or not */
	cxx_entity *nonconst = NULL;
	uint32_t n_nonconst = 0;

	c->none_viable = 0, c->call_obj = 0;
	/* no object written: this, in a member function (only when the candidates are members that differ in const) */
	if (!obj && set && c->this_type != CXX_TY_UNKNOWN && c->this_type) {
		bool any_const = false;

		for (cxx_entity *e = set; e; e = e->next)
			any_const |= e->fn_const;
		if (any_const)
			obj = c->this_const ? 2 : 1;
	}
	qty result = CXX_TY_UNKNOWN;
	bool same = true, unknown = false;

	if (!set) {
		/* a pointer to a function, a function's type */
		qty f = callee_type;

		if (f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == TY_PTR && !AST_TYPE(c->ast, f)->ext)
			f = AST_TYPE(c->ast, f)->base;
		cxx_et(c, f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == TY_FUNC ? AST_TYPE(c->ast, f)->base : CXX_TY_UNKNOWN, false);
		if (!c->template_depth)
			c->stats.calls++, c->stats.calls_typed += c->etype != CXX_TY_UNKNOWN, c->stats.why[CXX_WHY_NO_SET] += c->etype == CXX_TY_UNKNOWN;
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
			if (!c->template_depth)
				c->stats.calls++, c->stats.why[CXX_WHY_DEPENDENT]++;
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
			int d = nargs <= 16 ? cxx_ftemplate_deduce(c, e, xargs, nxargs, args, lval, nargs, &r) : -1;

			if (d < 0)
				unknown = true;	/* (a template vcc cannot deduce might be the best) */
			if (d <= 0)
				continue;
		} else {
			const ctype *ft;

			if (xargs)
				continue;	/* (f<...>: not a non-template) */
			if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != TY_FUNC)
				continue;
			ft = AST_TYPE(c->ast, e->ty);
			if (!((nargs <= ft->n && nargs + e->ndefaults >= ft->n) || ((ft->flags & TF_VARIADIC) && nargs >= ft->n)))
				continue;
			if (obj == 2 && !e->fn_const)
				continue;	/* (a const object: const members only) */
			r = ft->base;
			if (obj == 1 && !e->fn_const)
				n_nonconst++, nonconst = e;
			if (cxx_exact(c, ft, args, nargs))
				nexact++, exact = e;
		}
		if (!viable++)
			result = r, first = e;
		else if (r != result)
			same = false;
	}
	/* f() and f() const on an object that is not const: the one that is not const */
	if (nexact != 1 && !same && !unknown && n_nonconst == 1)
		nexact = 1, exact = nonconst;
	if (nexact == 1)
		result = AST_TYPE(c->ast, exact->ty)->base;	/* (an exact non-template wins over a template's equal match) */
	else if (!viable || !same || unknown) {
		c->none_viable = !viable && !unknown;
		if (!c->template_depth && !(c->none_viable && operator_call))
			c->stats.why[unknown ? c->why : !viable ? CXX_WHY_NONE_VIABLE : CXX_WHY_AMBIGUOUS]++;
		result = CXX_TY_UNKNOWN;
	} else if (result == CXX_TY_UNKNOWN && !c->template_depth)
		c->stats.why[CXX_WHY_RETURN]++;
	if (!c->template_depth && !(c->none_viable && operator_call))
		c->stats.calls++, c->stats.calls_typed += result != CXX_TY_UNKNOWN;
	if (result == CXX_TY_UNKNOWN && !c->template_depth && !(c->none_viable && operator_call) && getenv("VCC_CXX_WHY"))
		fprintf(stderr, "why %s %.*s/%u\n", nexact == 1 || (viable && same && !unknown) ? "return" : unknown ? "unknown" :
			!viable ? "none" : "ambiguous", (int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs);
	/* the function called (when known): an instance's member body is instantiated */
	{
		cxx_entity *called = nexact == 1 ? exact : viable == 1 && !unknown ? first : NULL;

		if (called && called->kind == CXX_ENTITY_VALUE)
			cxx_member_body_use(c, called);
	}
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
	/* (no candidate fits: the next place, in the end the built-in operator: &x, a = b, a, b) */
	set = cxx_lookup_in(c, cls, id, 0);
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, NULL, nargs, true);
		if (!c->none_viable)
			return true;
	}
	set = cxx_lookup(c, id);
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		all[0] = obj;
		for (uint32_t i = 0; i < nargs; i++)
			all[i + 1] = args[i];
		cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, all, NULL, nargs + 1, true);
		if (!c->none_viable)
			return true;
	}
	/* built-in: the address of an object; anything else on a class is not known (an implicit operator=,
	   a conversion to a built-in type) */
	if (vintern_ents[id].len == 9 && !memcmp(vintern_ents[id].s, "operator&", 9) && nargs == 0)
		return false;
	cxx_et(c, CXX_TY_UNKNOWN, false);
	return true;
}

VPRIV uint32_t cxx_ctor_id(void)
{
	static uint32_t id;

	return id ? id : (id = vintern_cstr(" ctor"));
}

VPRIV uint32_t cxx_dtor_id(void)
{
	static uint32_t id;

	return id ? id : (id = vintern_cstr(" dtor"));
}

/* is t an instance of a template named initializer_list (std::initializer_list<E>)? */
static bool cxx_is_initializer_list(cxx *c, qty t)
{
	cxx_scope *s;

	if (t == CXX_TY_UNKNOWN)
		return false;
	if (AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext)
		t = AST_TYPE(c->ast, t)->base;
	if (!cc_type_is_record(c->ast, t) || !(s = cxx_type_class_scope(c, QUNQ(t))) || !s->owner)
		return false;
	return vintern_ents[s->owner->name].len == 16 && !memcmp(vintern_ents[s->owner->name].s, "initializer_list", 16);
}

/*
 * An object of class type t is initialized from args (braced: a braced-init-list): the constructor
 * chosen (an initializer_list one first for braces), then the destructor; their bodies are
 * instantiated when t is an instance.  The expression's type is left as it was.
 */
VPRIV void cxx_ctor_use(cxx *c, qty t, const qty *args, const uint8_t *lval, uint32_t nargs, bool braced)
{
	qty et = c->etype;
	bool el = c->elval;
	cxx_entity *callee = c->ecallee, *set, *dtor;
	cxx_scope *cls;

	if (c->tentative || nargs == UINT32_MAX || t == CXX_TY_UNKNOWN || !(cls = cxx_type_class_scope(c, QUNQ(t))))
		return;
	cxx_type_require_complete(c, QUNQ(t));
	set = cxx_lookup_in(c, cls, cxx_ctor_id(), 0);
	if (set && braced) {
		/* T{a, b}: a constructor whose first parameter is std::initializer_list (the rest defaulted) */
		for (cxx_entity *e = set; e; e = e->next) {
			const ctype *ft;

			if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN)
				continue;
			ft = AST_TYPE(c->ast, e->ty);
			if (ft->n >= 1 && ft->n <= 1u + e->ndefaults && cxx_is_initializer_list(c, c->ast->params[ft->aux])) {
				cxx_member_body_use(c, e);
				set = NULL;
				break;
			}
		}
	}
	/* (from an object of the same class with no constructor that fits: the implicit copy or move) */
	if (set)
		cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, lval, nargs,
				   nargs == 1 && args[0] != CXX_TY_UNKNOWN && QUNQ(args[0]) == QUNQ(t));
	dtor = cxx_lookup_in(c, cls, cxx_dtor_id(), 0);
	if (dtor && dtor->kind == CXX_ENTITY_VALUE)
		cxx_member_body_use(c, dtor);
	c->etype = et, c->elval = el, c->ecallee = callee;
}

/* ---- integer constant expressions ---------------------------------------------------------- */

/* an expression of type t with the constant value v */
VPRIV void cxx_ec(cxx *c, qty t, int64_t v)
{
	cxx_et(c, t, false);
	if (t != CXX_TY_UNKNOWN) {
		int k = AST_TYPE_KIND(c->ast, t);
		uint64_t size = AST_TYPE(c->ast, t)->size;

		/* the value in t: bool is 0 or 1, a narrower integer wraps (sign- or zero-extended) */
		if (k == TY_BOOL)
			v = v != 0;
		else if (k >= TY_CHAR && k <= TY_ULLONG && size && size < 8) {
			int sh = (int)(64 - size * 8);

			v = cc_type_is_signed(c->ast, t) ? (int64_t)((uint64_t)v << sh) >> sh : (int64_t)(((uint64_t)v << sh) >> sh);
		}
	}
	c->evalue = v, c->econst = 1;
}

/* an integer literal's value (decimal, hex, octal, binary, ' separators, u/l suffixes) */
VPRIV bool cxx_number_value(const cxx_token *t, int64_t *out)
{
	uint64_t v = 0;
	uint32_t i = 0, base = 10, digits = 0;

	if (t->len > 1 && t->s[0] == '0') {
		if (t->s[1] == 'x' || t->s[1] == 'X')
			base = 16, i = 2;
		else if (t->s[1] == 'b' || t->s[1] == 'B')
			base = 2, i = 2;
		else
			base = 8, i = 1, digits = 1;
	}
	for (; i < t->len; i++) {
		char ch = t->s[i];
		uint32_t d;

		if (ch == '\'')
			continue;
		if (ch >= '0' && ch <= '9')
			d = (uint32_t)(ch - '0');
		else if (base == 16 && ((ch | 32) >= 'a' && (ch | 32) <= 'f'))
			d = (uint32_t)((ch | 32) - 'a' + 10);
		else
			break;
		if (d >= base)
			return false;
		v = v * base + d, digits++;
	}
	if (!digits)
		return false;
	for (; i < t->len; i++)
		if (t->s[i] != 'u' && t->s[i] != 'U' && t->s[i] != 'l' && t->s[i] != 'L')
			return false;	/* (a floating literal, a user-defined suffix) */
	*out = (int64_t)v;
	return true;
}

VPRIV bool cxx_binary_value(int op, int64_t l, int64_t r, bool uns, int64_t *v)
{
	uint64_t ul = (uint64_t)l, ur = (uint64_t)r;

	switch (op) {
	case LEX_TOKEN_PLUS: *v = (int64_t)(ul + ur); return true;
	case LEX_TOKEN_MINUS: *v = (int64_t)(ul - ur); return true;
	case LEX_TOKEN_STAR: *v = (int64_t)(ul * ur); return true;
	case LEX_TOKEN_SLASH:
		if (!r || (!uns && l == INT64_MIN && r == -1))
			return false;
		*v = uns ? (int64_t)(ul / ur) : l / r;
		return true;
	case LEX_TOKEN_PERCENT:
		if (!r || (!uns && l == INT64_MIN && r == -1))
			return false;
		*v = uns ? (int64_t)(ul % ur) : l % r;
		return true;
	case LEX_TOKEN_AMPERSAND: *v = l & r; return true;
	case LEX_TOKEN_PIPE: *v = l | r; return true;
	case LEX_TOKEN_CARET: *v = l ^ r; return true;
	case LEX_TOKEN_LESS_LESS:
		if (r < 0 || r > 63)
			return false;
		*v = (int64_t)(ul << r);
		return true;
	case LEX_TOKEN_GREATER_GREATER:
		if (r < 0 || r > 63)
			return false;
		*v = uns ? (int64_t)(ul >> r) : l >> r;
		return true;
	case LEX_TOKEN_EQUAL_EQUAL: *v = l == r; return true;
	case LEX_TOKEN_EXCLAMATION_EQUAL: *v = l != r; return true;
	case LEX_TOKEN_LESS: *v = uns ? ul < ur : l < r; return true;
	case LEX_TOKEN_GREATER: *v = uns ? ul > ur : l > r; return true;
	case LEX_TOKEN_LESS_EQUAL: *v = uns ? ul <= ur : l <= r; return true;
	case LEX_TOKEN_GREATER_EQUAL: *v = uns ? ul >= ur : l >= r; return true;
	case LEX_TOKEN_AMPERSAND_AMPERSAND: *v = l && r; return true;
	case LEX_TOKEN_PIPE_PIPE: *v = l || r; return true;
	}
	return false;
}

/*
 * clang's type trait builtins that depend on the type's form only (those that ask what a class can
 * do, __is_constructible and the like, are not known).  t: the type arguments.
 */
VPRIV bool cxx_trait_value(cxx *c, const char *n, size_t len, const qty *t, uint32_t nt, int64_t *v)
{
	const ctype *x;
	int k;
	bool ref;
#define IS(s) (len == sizeof s - 1 && !memcmp(n, s, len))

	if (nt == 2 && IS("__is_same")) {
		*v = t[0] == t[1];
		return true;
	}
	if (nt != 1)
		return false;
	x = AST_TYPE(c->ast, t[0]);
	k = x->kind;
	ref = k == TY_PTR && x->ext;
	if (IS("__is_void")) *v = k == TY_VOID;
	else if (IS("__is_integral")) *v = (k >= TY_BOOL && k <= TY_UINT128);
	else if (IS("__is_floating_point")) *v = (k >= TY_BFLOAT16 && k <= TY_FLOAT128);
	else if (IS("__is_arithmetic")) *v = (k >= TY_BOOL && k <= TY_FLOAT128);
	else if (IS("__is_pointer")) *v = k == TY_PTR && !x->ext;
	else if (IS("__is_reference")) *v = ref;
	else if (IS("__is_lvalue_reference")) *v = ref && x->ext == 1;
	else if (IS("__is_rvalue_reference")) *v = ref && x->ext == 2;
	else if (IS("__is_const")) *v = !ref && (QQ(t[0]) & Q_CONST) != 0;
	else if (IS("__is_volatile")) *v = !ref && (QQ(t[0]) & Q_VOLATILE) != 0;
	else if (IS("__is_array")) *v = k == TY_ARRAY;
	else if (IS("__is_function")) *v = k == TY_FUNC;
	else if (IS("__is_enum")) *v = k == TY_ENUM;
	else if (IS("__is_union")) *v = k == TY_UNION;
	else if (IS("__is_class")) *v = k == TY_STRUCT;
	else if (IS("__is_signed")) *v = (k >= TY_BFLOAT16 && k <= TY_FLOAT128) || (k >= TY_CHAR && k <= TY_UINT128 && cc_type_is_signed(c->ast, t[0]));
	else if (IS("__is_unsigned")) *v = k == TY_BOOL || (k >= TY_CHAR && k <= TY_UINT128 && !cc_type_is_signed(c->ast, t[0]));
	else if (IS("__is_fundamental")) *v = k <= TY_FLOAT128;
	else if (IS("__is_scalar")) *v = (k >= TY_BOOL && k <= TY_FLOAT128) || (k == TY_PTR && !x->ext) || k == TY_ENUM;
	else if (IS("__is_object")) *v = k != TY_FUNC && k != TY_VOID && !ref;
	else if (IS("__is_compound")) *v = k > TY_FLOAT128;
	else
		return false;
#undef IS
	return true;
}
