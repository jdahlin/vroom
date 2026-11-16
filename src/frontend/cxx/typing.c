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
static int cxx_class_assign_trivial(cxx *c, ast_qualified_type t, int depth);

VPRIV void cxx_et(cxx *c, ast_qualified_type t, bool lvalue)
{
	c->ecallee = NULL, c->xargs_set = 0, c->call_obj = 0, c->econst = 0;
	c->enode = 0;	/* (built by what follows the typing, where code is generated for it) */
	c->eobj_node = 0;
	c->edecl = t, c->edecl_name = 0;
	c->ename = 0, c->ename_missing = 0;	/* (a name only until something is done with it) */
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext) {
		/* a reference: its referent; an lvalue for T&, and for T&& only when it is a name (lvalue set by the
		   caller): a call or cast to T&& is an xvalue, which binds as an rvalue */
		c->etype = AST_TYPE(c->ast, t)->base, c->elval = AST_TYPE(c->ast, t)->ext == 1 || lvalue;
		return;
	}
	c->etype = t, c->elval = lvalue;
}

/* an integer or floating literal's type: C's rules (the suffix, the form) */
VPRIV ast_qualified_type cxx_number_type(cxx *c, const cxx_token *t)
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
		return cc_type_basic(f ? AST_TYPE_FLOAT : longs ? AST_TYPE_LONG_DOUBLE : AST_TYPE_DOUBLE);
	{
		/* the first type of the list [lex.icon] its value fits: int, long, long long (decimal); octal, hex and
		   binary also their unsigned ones; u: only the unsigned ones; l, ll: from long, long long (LP64) */
		unsigned __int128 v = 0;
		unsigned base = 10;
		uint32_t i = 0;
		bool big = false;

		if (hex)
			base = 16, i = 2;
		else if (t->len > 1 && t->s[0] == '0' && (t->s[1] == 'b' || t->s[1] == 'B'))
			base = 2, i = 2;
		else if (t->len > 1 && t->s[0] == '0')
			base = 8, i = 1;
		for (; i < t->len; i++) {
			char ch = t->s[i];
			unsigned d;

			if (ch == '\'')
				continue;
			if (ch >= '0' && ch <= '9')
				d = (unsigned)(ch - '0');
			else if (base == 16 && ch >= 'a' && ch <= 'f')
				d = (unsigned)(ch - 'a' + 10);
			else if (base == 16 && ch >= 'A' && ch <= 'F')
				d = (unsigned)(ch - 'A' + 10);
			else
				break;
			v = v * base + d;
			if (v >> 64)
				big = true;
		}
		bool dec = base == 10;

		if (!uns && longs == 0 && v <= 0x7fffffff)
			return cc_type_basic(AST_TYPE_INT);
		if (longs == 0 && v <= 0xffffffffu && (uns || !dec))
			return cc_type_basic(AST_TYPE_UNSIGNED_INT);
		if (!uns && longs <= 1 && v <= 0x7fffffffffffffffull && !big)
			return cc_type_basic(longs == 2 ? AST_TYPE_LONG_LONG : AST_TYPE_LONG);
		if (longs <= 1 && (uns || !dec) && !big)
			return cc_type_basic(AST_TYPE_UNSIGNED_LONG);
		if (!uns && v <= 0x7fffffffffffffffull && !big)
			return cc_type_basic(AST_TYPE_LONG_LONG);
		return cc_type_basic(uns || !dec ? AST_TYPE_UNSIGNED_LONG_LONG : AST_TYPE_LONG_LONG);	/* (too big for any: clang's extension, unsigned) */
	}
}

static bool cxx_is_arith(cxx *c, ast_qualified_type t)
{
	return t != CXX_TY_UNKNOWN && cc_type_is_arithmetic(c->ast, t);
}

static bool cxx_is_ptr(cxx *c, ast_qualified_type t)
{
	return t != CXX_TY_UNKNOWN && (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY);
}

static ast_qualified_type cxx_decay(cxx *c, ast_qualified_type t)
{
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY)
		return cc_type_pointer(c->ast, AST_TYPE(c->ast, t)->base);
	return t;
}

/* a binary operator's result (kind: the operator), from the operands' types */
/* std::name (a class of <compare> and the like, in libc++'s inline namespace too): its type, complete;
   CXX_TY_UNKNOWN if not declared */
VPRIV ast_qualified_type cxx_std_class(cxx *c, const char *name)
{
	cxx_entity *std = cxx_lookup_in(c, c->global, vintern_cstr("std"), 0), *e;

	while (std && !(std->kind == CXX_ENTITY_NAMESPACE && std->members))
		std = std->next;
	e = std ? cxx_lookup_in(c, std->members, vintern_cstr(name), 0) : NULL;
	while (e && e->kind != CXX_ENTITY_TYPE)
		e = e->next;
	if (!e || e->ty == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, e->ty))
		return CXX_TY_UNKNOWN;
	cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(e->ty));
	return AST_QUALIFIED_TYPE_UNQUALIFIED(e->ty);
}

VPRIV void cxx_binary_type(cxx *c, int op, ast_qualified_type l, ast_qualified_type r)
{
	/* two values of one enumeration compared: bool (a scoped one has no other built-in operators) */
	if (l != CXX_TY_UNKNOWN && r != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, l) == AST_TYPE_ENUM && AST_QUALIFIED_TYPE_UNQUALIFIED(l) == AST_QUALIFIED_TYPE_UNQUALIFIED(r) &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL || op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
	     op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL)) {
		cxx_et(c, cc_type_basic(AST_TYPE_BOOL), false);
		return;
	}
	/* an unscoped enumeration in arithmetic: promoted (to int here; a scoped one has no such operators) */
	if (l != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, l) == AST_TYPE_ENUM)
		l = cc_type_basic(AST_TYPE_INT);
	if (r != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, r) == AST_TYPE_ENUM)
		r = cc_type_basic(AST_TYPE_INT);
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: case LEX_TOKEN_EXCLAMATION_EQUAL: case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL:
	case LEX_TOKEN_GREATER: case LEX_TOKEN_GREATER_EQUAL: case LEX_TOKEN_AMPERSAND_AMPERSAND: case LEX_TOKEN_PIPE_PIPE:
		/* built in on arithmetic and pointers: bool (a class's operator: not resolved yet); std::nullptr_t (a
		   struct here) compares as a null pointer, == and != ([expr.eq]/2), && and || as false */
		{
			bool ln = l != CXX_TY_UNKNOWN && c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(l) == c->nullptr_ty;
			bool rn = r != CXX_TY_UNKNOWN && c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(r) == c->nullptr_ty;
			bool np = op != LEX_TOKEN_LESS && op != LEX_TOKEN_LESS_EQUAL && op != LEX_TOKEN_GREATER && op != LEX_TOKEN_GREATER_EQUAL;

			cxx_et(c, (cxx_is_arith(c, l) || cxx_is_ptr(c, l) || (ln && np)) && (cxx_is_arith(c, r) || cxx_is_ptr(c, r) || (rn && np)) ?
				  cc_type_basic(AST_TYPE_BOOL) : CXX_TY_UNKNOWN, false);
		}
		return;
	case LEX_TOKEN_LESS_LESS: case LEX_TOKEN_GREATER_GREATER:
		cxx_et(c, cxx_is_arith(c, l) && cxx_is_arith(c, r) ? cc_type_promote(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(l)) : CXX_TY_UNKNOWN, false);
		return;
	case CXX_TOKEN_SPACESHIP:
		/* built in: std::partial_ordering for floating point, std::strong_ordering for integers and pointers */
		if ((cxx_is_arith(c, l) || cxx_is_ptr(c, l)) && (cxx_is_arith(c, r) || cxx_is_ptr(c, r))) {
			bool fl = (cxx_is_arith(c, l) && !cc_type_is_integer(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(l))) || (cxx_is_arith(c, r) && !cc_type_is_integer(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(r)));

			cxx_et(c, cxx_std_class(c, fl ? "partial_ordering" : "strong_ordering"), false);
		} else
			cxx_et(c, CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_PLUS: case LEX_TOKEN_MINUS:
		if (cxx_is_ptr(c, l) && cxx_is_arith(c, r)) {
			cxx_et(c, AST_QUALIFIED_TYPE_UNQUALIFIED(cxx_decay(c, l)), false);
			return;
		}
		if (op == LEX_TOKEN_PLUS && cxx_is_arith(c, l) && cxx_is_ptr(c, r)) {
			cxx_et(c, AST_QUALIFIED_TYPE_UNQUALIFIED(cxx_decay(c, r)), false);
			return;
		}
		if (op == LEX_TOKEN_MINUS && cxx_is_ptr(c, l) && cxx_is_ptr(c, r)) {
			cxx_et(c, cc_type_basic(AST_TYPE_LONG), false);	/* ptrdiff_t */
			return;
		}
		__attribute__((fallthrough));
	case LEX_TOKEN_STAR: case LEX_TOKEN_SLASH: case LEX_TOKEN_PERCENT: case LEX_TOKEN_AMPERSAND: case LEX_TOKEN_CARET:
	case LEX_TOKEN_PIPE:
		cxx_et(c, cxx_is_arith(c, l) && cxx_is_arith(c, r) ? cc_type_arithmetic_conversion(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(l), AST_QUALIFIED_TYPE_UNQUALIFIED(r)) : CXX_TY_UNKNOWN,
		       false);
		return;
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
}

/* a unary operator's result */
VPRIV void cxx_unary_type(cxx *c, int op, ast_qualified_type t, bool lvalue)
{
	switch (op) {
	case LEX_TOKEN_AMPERSAND:
		cxx_et(c, lvalue && t != CXX_TY_UNKNOWN ? cxx_type_pointer(c, t, 0) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_STAR:
		cxx_et(c, cxx_is_ptr(c, t) ? AST_TYPE(c->ast, t)->base : CXX_TY_UNKNOWN, true);
		if (cxx_is_arith(c, t) && !c->template_depth)
			CXX_INVALID(c);	/* (*i of an integer: ill-formed, requires(T& t) { *t; } false for int) */
		return;
	case LEX_TOKEN_EXCLAMATION:
		/* (!x of a class: x contextually converted to bool, its explicit operator bool too; [expr.unary.op]/9) */
		cxx_et(c, cxx_is_arith(c, t) || cxx_is_ptr(c, t) || (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY) ?
		       cc_type_basic(AST_TYPE_BOOL) : CXX_TY_UNKNOWN, false);
		return;
	case LEX_TOKEN_MINUS: case LEX_TOKEN_PLUS: case LEX_TOKEN_TILDE:
		cxx_et(c, cxx_is_arith(c, t) ? cc_type_promote(c->ast, AST_QUALIFIED_TYPE_UNQUALIFIED(t)) : CXX_TY_UNKNOWN, false);
		if (op != LEX_TOKEN_PLUS && t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && !AST_TYPE(c->ast, t)->ext && !c->template_depth)
			CXX_INVALID(c);	/* (-p, ~p of a pointer) */
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
/* is the class d derived from the class b? */
static bool cxx_derives(cxx *c, ast_qualified_type d, ast_qualified_type b, int depth)
{
	cxx_scope *cls = depth < 8 ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(d)) : NULL;

	if (!cls)
		return false;
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *s = cls->used[i];

		if (!s || s->kind != CXX_SCOPE_CLASS || !s->owner || s->owner->ty == CXX_TY_UNKNOWN)
			continue;
		if (AST_QUALIFIED_TYPE_UNQUALIFIED(s->owner->ty) == AST_QUALIFIED_TYPE_UNQUALIFIED(b) || cxx_derives(c, s->owner->ty, b, depth + 1))
			return true;
	}
	return false;
}

/*
 * How an argument of type a converts to a parameter of type p: 0 exactly (identity, array or function
 * to pointer, a qualification), 1 a standard conversion (arithmetic, derived to base, to void *),
 * 2 perhaps (a class's constructor or conversion function, an unknown type), 3 not at all.
 */
static bool cxx_class_converts(cxx *c, cxx_scope *cls, int depth);
static int cxx_class_converts_to(cxx *c, cxx_scope *cls, ast_qualified_type to, int depth);
static bool cxx_class_converts_class(cxx *c, cxx_scope *cls, ast_qualified_type to, int depth);
static int cxx_class_constructs(cxx *c, ast_qualified_type to, const ast_qualified_type *args, const uint8_t *lval, uint32_t n, bool implicit);

/* is t a scoped enumeration's type (enum class) */
VPRIV bool cxx_enum_is_scoped(cxx *c, ast_qualified_type t)
{
	t = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
	for (uint32_t i = 0; i < c->nscoped_enums; i++)
		if (c->scoped_enums[i] == t)
			return true;
	return false;
}

/* is from to to an integral or floating-point promotion ([conv.prom], [conv.fpprom]): a narrow integer or bool
   to int, an unscoped enumeration to its underlying type or that type's promotion, float to double */
static bool cxx_is_promotion(cxx *c, ast_qualified_type from, ast_qualified_type to)
{
	cc *a = c->ast;
	int fk, tk;

	if (from == CXX_TY_UNKNOWN || to == CXX_TY_UNKNOWN)
		return false;
	from = AST_QUALIFIED_TYPE_UNQUALIFIED(from), to = AST_QUALIFIED_TYPE_UNQUALIFIED(to);
	fk = AST_TYPE_KIND(a, from), tk = AST_TYPE_KIND(a, to);
	if (from == to || tk == AST_TYPE_ENUM)
		return false;
	if (fk == AST_TYPE_FLOAT)
		return tk == AST_TYPE_DOUBLE;
	if (fk == AST_TYPE_ENUM) {
		ast_qualified_type b = AST_TYPE(a, from)->base;

		if (cxx_enum_is_scoped(c, from) || b == CXX_TY_UNKNOWN)
			return false;
		return AST_QUALIFIED_TYPE_UNQUALIFIED(b) == to || cc_type_promote(a, b) == to;
	}
	return fk >= AST_TYPE_BOOL && fk <= AST_TYPE_UNSIGNED_LONG_LONG && cc_type_promote(a, from) == to && to != from;
}

static int cxx_arg_rank0(cxx *c, ast_qualified_type p, ast_qualified_type a, bool lvalue, bool have_lvalue)
{
	const ctype *pt, *at;
	bool binds_object = false;	/* T& (not const): binds an object, never a temporary made by a constructor */

	if (p == CXX_TY_UNKNOWN || a == CXX_TY_UNKNOWN)
		return 2;
	pt = AST_TYPE(c->ast, p);
	if (pt->kind == AST_TYPE_POINTER && pt->ext) {
		binds_object = pt->ext == 1 && !(AST_QUALIFIED_TYPE_QUALIFIERS(pt->base) & AST_QUALIFIER_CONST);
		/* T&& does not bind an lvalue of T (or of a class derived from it: a temporary a conversion makes from
		   another type it binds, "text" to json&&); T& (not const) does not bind an rvalue */
		bool same = a != CXX_TY_UNKNOWN && (AST_QUALIFIED_TYPE_UNQUALIFIED(a) == AST_QUALIFIED_TYPE_UNQUALIFIED(pt->base) ||
						    (cc_type_is_record(c->ast, a) && cc_type_is_record(c->ast, pt->base) &&
						     cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a)) && cxx_derives(c, a, pt->base, 0)));

		if (have_lvalue && (pt->ext == 2 ? lvalue && (same || !cc_type_is_record(c->ast, pt->base)) : !lvalue && !(AST_QUALIFIED_TYPE_QUALIFIERS(pt->base) & AST_QUALIFIER_CONST) &&
				     !(AST_TYPE_KIND(c->ast, pt->base) == AST_TYPE_ARRAY && (AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c->ast, pt->base)->base) & AST_QUALIFIER_CONST))))	/* (const T(&)[N]: its elements' const) */
			return 3;
		p = pt->base;
		pt = AST_TYPE(c->ast, p);
	}
	at = AST_TYPE(c->ast, a);
	if (AST_QUALIFIED_TYPE_UNQUALIFIED(p) == AST_QUALIFIED_TYPE_UNQUALIFIED(a))
		return 0;
	/* nullptr converts to any pointer (and to bool), nothing else to nullptr_t */
	if (c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(a) == c->nullptr_ty)
		return (pt->kind == AST_TYPE_POINTER && !pt->ext) || pt->kind == AST_TYPE_BOOL ? 1 : cc_type_is_record(c->ast, p) ? 2 : 3;
	if (c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(p) == c->nullptr_ty)
		return 3;
	/* an array bound to a reference to array: the same length and element (its cv may differ) */
	if (pt->kind == AST_TYPE_ARRAY && at->kind == AST_TYPE_ARRAY)
		return pt->n == at->n && AST_QUALIFIED_TYPE_UNQUALIFIED(pt->base) == AST_QUALIFIED_TYPE_UNQUALIFIED(at->base) ? 0 : 3;
	if (((pt->kind >= AST_TYPE_BOOL && pt->kind <= AST_TYPE_FLOAT128) || pt->kind == AST_TYPE_ENUM) &&
	    ((at->kind >= AST_TYPE_BOOL && at->kind <= AST_TYPE_FLOAT128) || at->kind == AST_TYPE_ENUM))
		return pt->kind == AST_TYPE_ENUM || (at->kind == AST_TYPE_ENUM && cxx_enum_is_scoped(c, a)) ? 3 : 1;	/* (nothing converts to an enumeration; a scoped one converts to nothing) */
	if (pt->kind == AST_TYPE_POINTER && !pt->ext) {
		const ctype *pb = AST_TYPE(c->ast, pt->base);
		ast_qualified_type ab;

		if (at->kind == AST_TYPE_ARRAY)
			ab = at->base;
		else if (at->kind == AST_TYPE_FUNCTION)
			return AST_QUALIFIED_TYPE_UNQUALIFIED(pt->base) == AST_QUALIFIED_TYPE_UNQUALIFIED(a) ? 0 : 3;
		else if (at->kind == AST_TYPE_POINTER && !at->ext)
			ab = at->base;
		else if (at->kind == AST_TYPE_STRUCT || at->kind == AST_TYPE_UNION) {
			/* a class to a pointer: only through a conversion function */
			cxx_scope *as = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a));

			if (!as)
				return 2;
			cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a));
			{
				int k = cxx_class_converts_to(c, as, AST_QUALIFIED_TYPE_UNQUALIFIED(p), 0);

				return k > 0 ? 5 : k < 0 ? 2 : 3;
			}
		} else	/* (an integer: the literal 0 only, which the type does not tell) */
			return at->kind >= AST_TYPE_BOOL && at->kind <= AST_TYPE_UNSIGNED_LONG_LONG && !c->rank_no_literal && !(have_lvalue && lvalue) ? 2 : 3;
		if (AST_QUALIFIED_TYPE_UNQUALIFIED(pt->base) == AST_QUALIFIED_TYPE_UNQUALIFIED(ab))
			return 0;
		if (pb->kind == AST_TYPE_VOID)
			return 1;
		if (cc_type_is_record(c->ast, pt->base) && cc_type_is_record(c->ast, ab))
			return cxx_derives(c, ab, pt->base, 0) ? 1 : 3;
		return 3;
	}
	if (cc_type_is_record(c->ast, p) && cc_type_is_record(c->ast, a) && cxx_derives(c, a, p, 0))
		return 1;
	if (cc_type_is_record(c->ast, p) || cc_type_is_record(c->ast, a)) {
		/* through a class: a conversion function of the argument's class or a constructor of the
		   parameter's.  Neither declared: no conversion. */
		cxx_scope *as = cc_type_is_record(c->ast, a) ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a)) : NULL;
		cxx_scope *ps = cc_type_is_record(c->ast, p) && !binds_object ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(p)) : NULL;
		bool by_ctor = false, by_conv = false;

		if (cc_type_is_record(c->ast, a)) {
			if (!as)
				return 2;
			cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(a));
			by_conv = cxx_class_converts(c, as, 0);
			/* to something that is not a class: one of its conversion functions' types converts to it, or not */
			if (by_conv && !cc_type_is_record(c->ast, p)) {
				int k = cxx_class_converts_to(c, as, AST_QUALIFIED_TYPE_UNQUALIFIED(p), 0);

				if (k >= 0)
					return k ? 5 : 3;
			}
			/* to a class: only a conversion function to a class type can give one */
			if (by_conv && cc_type_is_record(c->ast, p) && !cxx_class_converts_class(c, as, AST_QUALIFIED_TYPE_UNQUALIFIED(p), 0))
				by_conv = false;
		}
		if (cc_type_is_record(c->ast, p) && !binds_object) {
			if (!ps)
				return 2;
			cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(p));
			if (ps->has_dependent_base)
				return 2;
			for (cxx_entity *e = cxx_lookup_in(c, ps, cxx_ctor_id(), 0); e; e = e->next)
				by_ctor |= e->member_of == ps;
		}
		if (!by_ctor && !by_conv)
			return 3;
		/* (std::initializer_list from anything but a braced list, which is ranked apart: no conversion makes one) */
		if (!by_conv && cxx_is_initializer_list(c, p))
			return 3;
		/* T(const T&) for an argument that is not a T: converting it to T first is this conversion again */
		if (c->constructing && AST_QUALIFIED_TYPE_UNQUALIFIED(p) == c->constructing && !by_conv)
			return 3;
		/* (within a user-defined conversion being tried, a constructor's parameter: no second one,
		   [over.best.ics]/4 -- int * is no It<const T> for It's It(const It<const T>&)) */
		if (by_ctor && !by_conv && c->construct_depth >= 1)
			return 3;
		/* a constructor of the parameter's class takes the argument (implicitly): a user-defined
		   conversion (5: worse than any standard one) */
		if (by_ctor && !by_conv && c->construct_depth < 2) {
			uint8_t lv = lvalue;
			int r;

			uint8_t nl = c->rank_no_literal;

			c->construct_depth++;
			c->no_literal = nl;
			r = cxx_class_constructs(c, AST_QUALIFIED_TYPE_UNQUALIFIED(p), &a, &lv, 1, true);
			c->construct_depth--;
			c->rank_no_literal = nl;
			if (r >= 0)
				return r ? 5 : 3;
		}
		return 2;
	}
	return 3;
}

/*
 * The rank used to compare candidates: 0 exact, 1 exact but bound to a reference that adds const (f(C&)
 * is better than f(const C&) for an object that is not const, f(T&&) than f(const T&) for an rvalue),
 * 2 a standard conversion, 4 perhaps, 6 not at all.
 */
static int cxx_arg_rank(cxx *c, ast_qualified_type p, ast_qualified_type a, bool lvalue, bool have_lvalue)
{
	int r = cxx_arg_rank0(c, p, a, lvalue, have_lvalue) * 2;

	if (r == 10)
		return 3;	/* a user-defined conversion */

	if (!r && p != CXX_TY_UNKNOWN && a != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, p) == AST_TYPE_POINTER && AST_TYPE(c->ast, p)->ext &&
	    (AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c->ast, p)->base) & ~AST_QUALIFIED_TYPE_QUALIFIERS(a) & AST_QUALIFIER_CONST))
		r = 1;
	return r;
}

/*
 * A call's type.  set: the functions the callee names (NULL: callee_type, a function or a pointer
 * to one).  The candidates are the non-templates whose arity fits and the templates whose
 * arguments deduce; the call's type is known when one exact non-template matches, or when every
 * viable candidate returns the same type (a simplification of overload resolution).
 */
static void cxx_call_type_impl(cxx *c, cxx_entity *set, ast_qualified_type callee_type, const cxx_targ *xargs, uint32_t nxargs, const ast_qualified_type *args,
			       const uint8_t *lval, uint32_t nargs, bool operator_call);

VPRIV void cxx_call_type(cxx *c, cxx_entity *set, ast_qualified_type callee_type, const ast_qualified_type *args, const uint8_t *lval, uint32_t nargs)
{
	cxx_call_type_impl(c, set, callee_type, NULL, UINT32_MAX, args, lval, nargs, false);
}

/* f<xargs>(args): only templates are candidates, their first parameters given */
VPRIV void cxx_call_type_x(cxx *c, cxx_entity *set, const cxx_targ *xargs, uint32_t nxargs, const ast_qualified_type *args, const uint8_t *lval,
			   uint32_t nargs)
{
	cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, xargs, nxargs, args, lval, nargs, false);
}

static void cxx_call_type_impl1(cxx *c, cxx_entity *set, ast_qualified_type callee_type, const cxx_targ *xargs, uint32_t nxargs, const ast_qualified_type *args,
				const uint8_t *lval, uint32_t nargs, bool operator_call, uint8_t *verdict);

/*
 * (What deciding the call left in the validity flags -- candidates that failed to deduce, the classes and
 * signatures instantiated on the way -- is not the expression's: only the call's own verdict is, no
 * function fits (set by the call itself) or its arguments are a pattern's placeholders.)
 */
/* what the expression just parsed is, when it is an untyped name of functions (else: none) */
VPRIV void cxx_argset_take(cxx *c, cxx_argset *as)
{
	as->set = NULL, as->nx = 255;
	if (c->etype != CXX_TY_UNKNOWN || !c->ecallee || c->tentative || c->template_depth)
		return;
	as->set = c->ecallee;
	if (c->xargs_set) {
		if (c->nxargs > 4) {
			as->set = NULL;
			return;
		}
		as->nx = (uint8_t)c->nxargs;
		memcpy(as->xa, c->xargs, c->nxargs * sizeof *c->xargs);
	}
}

/*
 * The function of as's set whose type a parameter of type p (a pointer or reference to a function type)
 * takes ([over.over]): a function of that type, or a template's specialization deduced from it
 * ([temp.deduct.funcaddr], as a call with the target's parameters as arguments).  NULL: none.
 */
VPRIV cxx_entity *cxx_function_for_target(cxx *c, const cxx_argset *as, ast_qualified_type p)
{
	ast_qualified_type f;
	const ctype *ft;

	if (!as || !as->set || p == CXX_TY_UNKNOWN)
		return NULL;
	f = AST_TYPE_KIND(c->ast, p) == AST_TYPE_POINTER ? AST_TYPE(c->ast, p)->base : p;
	if (f == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, f) != AST_TYPE_FUNCTION)
		return NULL;
	f = AST_QUALIFIED_TYPE_UNQUALIFIED(f);
	ft = AST_TYPE(c->ast, f);
	if (as->nx == 255)
		for (cxx_entity *e = as->set; e; e = e->next)
			if (e->kind == CXX_ENTITY_VALUE && e->ty != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(e->ty) == f && (!e->member_of || e->fn_static))
				return e;
	if (ft->n > 16 || (ft->flags & AST_TYPE_FLAG_VARIADIC))
		return NULL;
	for (cxx_entity *e = as->set; e; e = e->next) {
		ast_qualified_type at[16], ret;
		uint8_t lv[16];
		cxx_entity *inst, *was = c->call_instance;
		int r;

		if (e->kind != CXX_ENTITY_FUNCTION_TEMPLATE || (e->member_of && !e->fn_static))
			continue;
		for (uint32_t i = 0; i < ft->n; i++) {
			ast_qualified_type q = c->ast->params[ft->aux + i];

			if (q != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, q) == AST_TYPE_POINTER && AST_TYPE(c->ast, q)->ext)
				at[i] = AST_TYPE(c->ast, q)->base, lv[i] = AST_TYPE(c->ast, q)->ext == 1;
			else
				at[i] = q, lv[i] = 0;
		}
		r = cxx_ftemplate_deduce(c, e, as->nx == 255 ? NULL : as->xa, as->nx == 255 ? UINT32_MAX : as->nx, at, lv, ft->n, &ret, true);
		inst = c->call_instance;
		c->call_instance = was;
		if (r == 1 && inst && inst->ty != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(inst->ty) == f)
			return inst;
	}
	return NULL;
}

static void cxx_call_type_impl(cxx *c, cxx_entity *set, ast_qualified_type callee_type, const cxx_targ *xargs, uint32_t nxargs, const ast_qualified_type *args,
			       const uint8_t *lval, uint32_t nargs, bool operator_call)
{
	uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;

	uint8_t verdict = 0;	/* 1: no function fits; 2: validity not known (placeholder arguments) */
	/* (a binary operator's pending right operand: this resolution's, not one it makes on the way) */
	cxx_argset outer = c->op_rset_cur;

	c->op_rset_cur = c->op_rset, c->op_rset.set = NULL;
	cxx_call_type_impl1(c, set, callee_type, xargs, nxargs, args, lval, nargs, operator_call, &verdict);
	c->op_rset = c->op_rset_cur, c->op_rset_cur = outer;
	c->einvalid = invalid | (verdict & 1), c->eunsure = unsure | ((verdict & 2) != 0);
	c->member_missing = missing, c->einvalid_unsure = adl;
}

static void cxx_call_type_impl1(cxx *c, cxx_entity *set, ast_qualified_type callee_type, const cxx_targ *xargs, uint32_t nxargs, const ast_qualified_type *args,
				const uint8_t *lval, uint32_t nargs, bool operator_call, uint8_t *verdict)
{
	static const uint8_t none[16];
	cxx_entity *first = NULL;
	uint32_t viable = 0;
	uint8_t skip_explicit = c->skip_explicit;
	bool no_literal = c->no_literal;
	uint8_t obj = c->call_obj;	/* a member call's object: const or not */
	uint8_t obj_rv = c->call_obj_rv;	/* ... an rvalue (1) or an lvalue (2): f() & and f() && */
	/* the arguments the last argument list parsed (its integer constants 0 known: c->carg_zero) */
	bool own_args = args && nargs && nargs <= 16 && nargs == c->ncargs && c->cargs && (args == c->cargs || !memcmp(args, c->cargs, nargs * sizeof *args));
	uint32_t own_zero = c->carg_zero, own_brace[CXX_MAX_ARGS];	/* (taken now: deducing and instantiating parse argument lists of their own) */

	memcpy(own_brace, c->carg_brace, sizeof own_brace);
	cxx_argset own_sets[16], last_set = c->op_rset_cur;

	if (own_args)
		memcpy(own_sets, c->carg_sets, sizeof own_sets);
	bool have_lval = lval != NULL;

	c->none_viable = 0, c->call_obj = 0, c->call_obj_rv = 0;
	c->skip_explicit = 0, c->no_literal = 0;	/* (this call's; the calls made while deducing are their own) */
	/* no object written: this, in a member function (only when the candidates are members that differ in const) */
	if (!obj && set && c->this_type != CXX_TY_UNKNOWN && c->this_type) {
		bool any_const = false;

		for (cxx_entity *e = set; e; e = e->next)
			any_const |= (e->fn_const & 3) != 0;
		if (any_const)
			obj = c->this_const ? 2 : 1;
	}
	ast_qualified_type result = CXX_TY_UNKNOWN;
	bool same = true, unknown = false;

	if (!set) {
		/* a pointer to a function, a function's type */
		ast_qualified_type f = callee_type;

		if (f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == AST_TYPE_POINTER && !AST_TYPE(c->ast, f)->ext)
			f = AST_TYPE(c->ast, f)->base;
		cxx_et(c, f != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, f) == AST_TYPE_FUNCTION ? AST_TYPE(c->ast, f)->base : CXX_TY_UNKNOWN, false);
		if (!c->template_depth)
			c->stats.calls++, c->stats.calls_typed += c->etype != CXX_TY_UNKNOWN, c->stats.why[CXX_WHY_NO_SET] += c->etype == CXX_TY_UNKNOWN;
		if (!c->template_depth && c->etype == CXX_TY_UNKNOWN && getenv("VCC_CXX_WHY_NOSET")) {
			/* the callee names no function and has no function type: the call's tokens */
			uint32_t b = c->pos, depth = 0;

			while (b > 0 && c->pos - b < 40) {	/* (back to the call's opening parenthesis, then a few before) */
				b--;
				if (c->tok[b].kind == LEX_TOKEN_RIGHT_PAREN)
					depth++;
				else if (c->tok[b].kind == LEX_TOKEN_LEFT_PAREN && !--depth)
					break;
			}
			fprintf(stderr, "no callee (%s):", c->unevaluated ? "unevaluated" : "evaluated");
			for (uint32_t i = b > 8 ? b - 8 : 0; i < b + 3 && i < c->ntok; i++)
				fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
		}
		return;
	}
	if (!lval)
		lval = none;
	ast_qualified_type acopy[16];
	uint8_t lcopy[16];

	if (nargs <= 16) {	/* (instantiating a body reuses c->cargs) */
		memcpy(acopy, args, nargs * sizeof *args), memcpy(lcopy, lval, nargs);
		args = acopy, lval = lcopy;
	}
	/* the viable candidates, each argument's conversion ranked */
	struct cand { cxx_entity *e; ast_qualified_type r; uint8_t rank[16]; uint8_t ranked, tmpl; ast_qualified_type pt[16]; } cand[64];
	cxx_entity *unk[8];	/* the templates whose deduction is not known */
	uint32_t nunk = 0;
	const char *tn = getenv("VCC_CXX_CAND");	/* trace the candidates of calls to this name */
	bool trace = tn && !c->template_depth && strlen(tn) == vintern_ents[set->name].len && !memcmp(tn, vintern_ents[set->name].s, strlen(tn));
	uint32_t ncand = 0;
	int best = -1;

	/* a call on a pattern's placeholders: a type perhaps, but whether it is valid depends on what they stand
	   for (a partial specialization's X<T, decltype(f(std::declval<T>()))> is substituted when matching) */
	for (uint32_t i = 0; i < nargs && i < 16 && !c->tentative; i++)
		if (args[i] != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, args[i]))
			*verdict |= 2;
	if (trace) {
		fprintf(stderr, "call %.*s/%u implicit=%d constructing=%d tentative=%d unevaluated=%d args:", (int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs, skip_explicit, c->constructing != 0,
			c->tentative, c->unevaluated);
		for (uint32_t i = 0; i < nargs && i < 16; i++) {
			char buf[160];
			size_t len = 0;

			if (args[i] != CXX_TY_UNKNOWN && cxx_type_spell(c, args[i], buf, sizeof buf, &len))
				fprintf(stderr, " [%.*s]%s", (int)len, buf, lval[i] ? "&" : "");
			else
				fprintf(stderr, " [%s kind %d ext %d placeholder %d record %d]", args[i] == CXX_TY_UNKNOWN ? "?" : "unspellable",
					args[i] == CXX_TY_UNKNOWN ? -1 : (int)AST_TYPE_KIND(c->ast, args[i]),
					args[i] == CXX_TY_UNKNOWN ? -1 : (int)AST_TYPE(c->ast, args[i])->ext,
					args[i] != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, args[i]),
					args[i] != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, args[i]));
		}
		if (xargs && nxargs != UINT32_MAX) {
			fprintf(stderr, " explicit:");
			for (uint32_t i = 0; i < nxargs; i++) {
				char buf[160];
				size_t len = 0;

				if (xargs[i].kind == 1 && xargs[i].t != CXX_TY_UNKNOWN && cxx_type_spell(c, xargs[i].t, buf, sizeof buf, &len))
					fprintf(stderr, " <%.*s>", (int)len, buf);
				else
					fprintf(stderr, " <kind %u%s>", xargs[i].kind, xargs[i].kind == 1 && xargs[i].t == CXX_TY_UNKNOWN ? " unknown" :
						xargs[i].kind == 1 && cxx_type_has_placeholder(c, xargs[i].t) ? " placeholder" : "");
			}
		}
		fputc('\n', stderr);
	}
	for (cxx_entity *e = set; e; e = e->next) {
		ast_qualified_type r;
		const ctype *sig = NULL;
		struct cand *k;

		if (e->kind == CXX_ENTITY_DEPENDENT) {
			if (!c->template_depth)
				c->stats.calls++, c->stats.why[CXX_WHY_DEPENDENT]++;
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
		if (skip_explicit && e->fn_explicit && !(skip_explicit == 2 && e->fn_explicit == 2)) {
			/* (copy-initialization: not through an explicit constructor; one explicit(cond) vcc cannot decide
			   leaves the call unknown only when it is viable otherwise: pair's, for a vector's iterator) */
			if (e->fn_explicit == 2) {
				bool viable_here = true;

				if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
					ast_qualified_type r0;

					viable_here = nargs <= 16 && cxx_ftemplate_deduce(c, e, xargs, nxargs, args, lval, nargs, &r0, false) != 0;
				} else if (e->kind == CXX_ENTITY_VALUE && e->ty != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, e->ty) == AST_TYPE_FUNCTION) {
					const ctype *ft0 = AST_TYPE(c->ast, e->ty);

					viable_here = (ft0->flags & AST_TYPE_FLAG_VARIADIC) ? nargs + e->ndefaults >= ft0->n : nargs <= ft0->n && nargs + e->ndefaults >= ft0->n;
				}
				unknown |= viable_here;
			}
			continue;
		}
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE && !e->fn_static && ((obj == 2 && !(e->fn_const & 1)) ||
		    (obj_rv == 1 && (e->fn_const & 4)) || (obj_rv == 2 && (e->fn_const & 8))))
			continue;	/* (a const object: const member templates only) */
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
			int d = nargs <= 16 ? cxx_ftemplate_deduce(c, e, xargs, nxargs, args, lval, nargs, &r, false) : -1;

			if (trace) {
				fprintf(stderr, "cand %.*s/%u template: deduce %d why %u ret %s:", (int)vintern_ents[set->name].len, vintern_ents[set->name].s,
					nargs, d, d <= 0 ? c->why : 0, d > 0 && r != CXX_TY_UNKNOWN ? "known" : "unknown");
				for (uint32_t i = cxx_ftemplate_pos(e), k = 0; i && k < 60 && i < c->ntok; i++, k++)
					fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
				fputc('\n', stderr);
			}
			if (d < 0)
				unknown = true;	/* (a template vcc cannot deduce might be the best) */
			if (d < 0 && nunk < 8)
				unk[nunk++] = e;
			if (d <= 0)
				continue;
			/* its parameters with the arguments bound */
			if (c->bound_sig != CXX_TY_UNKNOWN && ((AST_TYPE(c->ast, c->bound_sig)->flags & AST_TYPE_FLAG_VARIADIC) ||
								AST_TYPE(c->ast, c->bound_sig)->n >= nargs))
				sig = AST_TYPE(c->ast, c->bound_sig);	/* (f(...) too: an argument for the ellipsis ranks last) */
			else if (c->bound_sig != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, c->bound_sig) == AST_TYPE_FUNCTION &&
				 AST_TYPE(c->ast, c->bound_sig)->n < nargs && !cxx_ftemplate_has_packs(e))
				continue;	/* (fewer parameters than arguments, no pack, no ...: not viable; min(initializer_list<T>) for min(a, b)) */
		} else {
			const ctype *ft;

			if (xargs)
				continue;	/* (f<...>: not a non-template) */
			if (e->kind == CXX_ENTITY_VALUE && e->ty == CXX_TY_UNKNOWN && e->has_ret_pattern && e->late)
				cxx_late_deduce(c, e->late);	/* (auto f() of an instance: its type from its body, now) */
			if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != AST_TYPE_FUNCTION)
				continue;
			ft = AST_TYPE(c->ast, e->ty);
			if (!((nargs <= ft->n && nargs + e->ndefaults >= ft->n) || ((ft->flags & AST_TYPE_FLAG_VARIADIC) && nargs >= ft->n)))
				continue;
			if (obj == 2 && !(e->fn_const & 1) && !e->fn_static)
				continue;	/* (a const object: const members only) */
			if ((obj_rv == 1 && (e->fn_const & 4)) || (obj_rv == 2 && (e->fn_const & 8)))
				continue;	/* (f() & on an rvalue, f() && on an lvalue) */
			r = ft->base;
			sig = ft;
		}
		if (ncand == 64) {
			unknown = true;
			continue;
		}
		k = &cand[ncand];
		k->e = e, k->r = r, k->tmpl = e->kind == CXX_ENTITY_FUNCTION_TEMPLATE, k->ranked = sig != NULL && nargs <= 16;
		if (k->ranked) {
			bool fits = true;

			for (uint32_t a = 0; a < nargs && fits; a++) {
				/* (an argument that is no integer constant 0 is no null pointer constant: the call's own
				   arguments say which are; an argument for the ...: any) */
				c->rank_no_literal = no_literal || (own_args && a < 16 && !((own_zero >> a) & 1));
				if (args[a] == CXX_TY_UNKNOWN && a < sig->n) {
					/* a name of functions for a parameter of type pointer or reference to function: the one
					   it takes, exactly; none: not viable */
					const cxx_argset *as = own_args && a < 16 && own_sets[a].set ? &own_sets[a] :
							       !own_args && a + 1 == nargs && last_set.set ? &last_set : NULL;
					ast_qualified_type P = c->ast->params[sig->aux + a], F = P;

					if (as && P != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, P) == AST_TYPE_POINTER)
						F = AST_TYPE(c->ast, P)->base;
					if (as && F != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, F) == AST_TYPE_FUNCTION) {
						k->rank[a] = cxx_function_for_target(c, as, P) ? 0 : 6;
						k->pt[a] = P;
						fits = k->rank[a] < 6;
						continue;
					}
				}
				if (own_args && a < 16 && own_brace[a] && a < sig->n) {
					/* a braced list (its type here its last element's): initializer_list<E> takes it as it is,
					   a class by a constructor, a scalar only from one element ([over.ics.list]) */
					ast_qualified_type P = c->ast->params[sig->aux + a];

					if (P != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, P) == AST_TYPE_POINTER && AST_TYPE(c->ast, P)->ext)
						P = AST_TYPE(c->ast, P)->base;
					if (P == CXX_TY_UNKNOWN)
						k->rank[a] = 4;
					else if (cxx_is_initializer_list(c, P))
						k->rank[a] = 0;
					else if (cc_type_is_record(c->ast, P) && AST_TYPE_KIND(c->ast, P) != AST_TYPE_ARRAY)
						k->rank[a] = 3;
					else if (own_brace[a] < c->ntok && c->tok[own_brace[a]].kind == LEX_TOKEN_LEFT_BRACE &&
						 own_brace[a] + 1 < c->ntok && c->tok[own_brace[a] + 1].kind == LEX_TOKEN_RIGHT_BRACE)
						k->rank[a] = 0;	/* (a scalar from {}: value-initialized, an identity conversion) */
					else
						k->rank[a] = 6;	/* (a scalar from {x}: rare, not told apart here) */
					k->pt[a] = c->ast->params[sig->aux + a];
					fits = k->rank[a] < 6;
					continue;
				}
				k->rank[a] = a < sig->n ? (uint8_t)cxx_arg_rank(c, c->ast->params[sig->aux + a], args[a], lval[a], have_lval) : 5;	/* (...: last) */
				k->pt[a] = a < sig->n ? c->ast->params[sig->aux + a] : CXX_TY_UNKNOWN;
				fits = k->rank[a] < 6;
			}
			c->rank_no_literal = 0;
			if (!fits)
				continue;	/* (T&& from an lvalue, a pointer from a class: not viable) */
		}
		if (trace) {
			fprintf(stderr, "cand %.*s/%u %s const=%u ranked=%u ranks", (int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs,
				k->tmpl ? "template" : "plain", e->fn_const, k->ranked);
			for (uint32_t a = 0; a < nargs && k->ranked; a++)
				fprintf(stderr, " %u", k->rank[a]);
			fprintf(stderr, " obj=%u\n", obj);
		}
		{
			/* a redeclaration of a candidate already here (the same function: its type, its scope, not a
			   template's), the implicit operator new and <new>'s: one candidate */
			bool dup = false;

			for (uint32_t d = 0; d < ncand && !dup; d++)
				if (k->tmpl && cand[d].tmpl && e->ftmpl && cand[d].e->ftmpl == e->ftmpl)
					dup = true;	/* (one function template found twice: using std::__math::signbit, and std's) */
				else if (!k->tmpl && !cand[d].tmpl && cand[d].e->ty == e->ty && e->ty != CXX_TY_UNKNOWN &&
				    cand[d].e->member_of == e->member_of && cand[d].e->home == e->home && cand[d].e->name == e->name &&
				    cand[d].e->fn_const == e->fn_const) {	/* (f() and f() const: two functions of one type here) */
					dup = true;
					if (!cand[d].e->sym && e->sym)
						cand[d].e = e;
				}
			if (dup)
				continue;
		}
		ncand++;
		if (!viable++)
			result = r, first = e;
		else if (r != result)
			same = false;
	}
	/*
	 * The best: at least as good as every other for each argument, and better for one; then a
	 * non-template over a template, and on an object that is not const f() over f() const.  A
	 * candidate whose parameters are not known cannot be compared.
	 */
	for (uint32_t a = 0; a < ncand && best < 0; a++) {
		bool wins = cand[a].ranked;

		for (uint32_t b = 0; b < ncand && wins; b++) {
			bool better = false, worse = false;

			if (a == b)
				continue;
			if (!cand[b].ranked) {
				wins = false;
				break;
			}
			for (uint32_t n = 0; n < nargs; n++) {
				better |= cand[a].rank[n] < cand[b].rank[n];
				worse |= cand[a].rank[n] > cand[b].rank[n];
			}
			if (!better && !worse) {
				/* the same conversion of an argument to T: binding T&& is better than const T& when the
				   argument (or what a conversion makes of it, a temporary) is an rvalue [over.ics.rank] */
				for (uint32_t n = 0; n < nargs && n < 16; n++) {
					ast_qualified_type pa = cand[a].pt[n], pb = cand[b].pt[n];

					if (pa == CXX_TY_UNKNOWN || pb == CXX_TY_UNKNOWN || pa == pb || cand[a].rank[n] != cand[b].rank[n] ||
					    AST_TYPE_KIND(c->ast, pa) != AST_TYPE_POINTER || AST_TYPE_KIND(c->ast, pb) != AST_TYPE_POINTER ||
					    !AST_TYPE(c->ast, pa)->ext || !AST_TYPE(c->ast, pb)->ext ||
					    AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c->ast, pa)->base) != AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c->ast, pb)->base))
						continue;
					bool rv = cand[a].rank[n] == 3 || (lval && !lval[n]);	/* (3: through a conversion, a temporary) */

					if (!rv)
						continue;
					if (AST_TYPE(c->ast, pa)->ext == 2 && AST_TYPE(c->ast, pb)->ext == 1)
						better = true;
					else if (AST_TYPE(c->ast, pa)->ext == 1 && AST_TYPE(c->ast, pb)->ext == 2)
						worse = true;
				}
			}
			if (!better && !worse) {
				/* T * against cv T *: the one adding fewer qualifiers is better ([over.ics.rank]/3.2.6:
				   const base * over const volatile base *, libc++'s __cxx_atomic_load) */
				for (uint32_t n = 0; n < nargs && n < 16; n++) {
					ast_qualified_type pa = cand[a].pt[n], pb = cand[b].pt[n];

					if (pa == CXX_TY_UNKNOWN || pb == CXX_TY_UNKNOWN || cand[a].rank[n] != cand[b].rank[n] ||
					    AST_TYPE_KIND(c->ast, pa) != AST_TYPE_POINTER || AST_TYPE_KIND(c->ast, pb) != AST_TYPE_POINTER ||
					    AST_TYPE(c->ast, pa)->ext != AST_TYPE(c->ast, pb)->ext)
						continue;
					pa = AST_TYPE(c->ast, pa)->base, pb = AST_TYPE(c->ast, pb)->base;
					if (pa == CXX_TY_UNKNOWN || pb == CXX_TY_UNKNOWN || AST_QUALIFIED_TYPE_UNQUALIFIED(pa) != AST_QUALIFIED_TYPE_UNQUALIFIED(pb) || AST_QUALIFIED_TYPE_QUALIFIERS(pa) == AST_QUALIFIED_TYPE_QUALIFIERS(pb))
						continue;
					if ((AST_QUALIFIED_TYPE_QUALIFIERS(pa) & AST_QUALIFIED_TYPE_QUALIFIERS(pb)) == AST_QUALIFIED_TYPE_QUALIFIERS(pa))
						better = true;
					else if ((AST_QUALIFIED_TYPE_QUALIFIERS(pa) & AST_QUALIFIED_TYPE_QUALIFIERS(pb)) == AST_QUALIFIED_TYPE_QUALIFIERS(pb))
						worse = true;
				}
			}
			if (!better && !worse && args) {
				/* the same rank, a promotion against another conversion: the promotion ([over.ics.rank]/4.2:
				   os << e of an unscoped enumeration is operator<<(int), not the char16_t one) */
				for (uint32_t n = 0; n < nargs && n < 16; n++) {
					ast_qualified_type pa = cand[a].pt[n], pb = cand[b].pt[n];

					if (pa == CXX_TY_UNKNOWN || pb == CXX_TY_UNKNOWN || args[n] == CXX_TY_UNKNOWN || cand[a].rank[n] != 2 || cand[b].rank[n] != 2)
						continue;
					if (AST_TYPE_KIND(c->ast, pa) == AST_TYPE_POINTER && AST_TYPE(c->ast, pa)->ext)
						pa = AST_TYPE(c->ast, pa)->base;
					if (AST_TYPE_KIND(c->ast, pb) == AST_TYPE_POINTER && AST_TYPE(c->ast, pb)->ext)
						pb = AST_TYPE(c->ast, pb)->base;
					bool ppa = cxx_is_promotion(c, args[n], pa), ppb = cxx_is_promotion(c, args[n], pb);

					if (ppa && !ppb)
						better = true;
					else if (ppb && !ppa)
						worse = true;
				}
				if (better && worse)
					better = worse = false;
			}
			if (!better && !worse && args) {
				/* derived to base: to a nearer base is better (B over A for a C : B : A [over.ics.rank]; json's
				   priority_tag<N>) */
				for (uint32_t n = 0; n < nargs && n < 16; n++) {
					ast_qualified_type pa = cand[a].pt[n], pb = cand[b].pt[n], at = AST_QUALIFIED_TYPE_UNQUALIFIED(args[n]);

					if (pa == CXX_TY_UNKNOWN || pb == CXX_TY_UNKNOWN || at == CXX_TY_UNKNOWN || cand[a].rank[n] != cand[b].rank[n])
						continue;
					if (AST_TYPE_KIND(c->ast, pa) == AST_TYPE_POINTER && AST_TYPE(c->ast, pa)->ext)
						pa = AST_TYPE(c->ast, pa)->base;
					if (AST_TYPE_KIND(c->ast, pb) == AST_TYPE_POINTER && AST_TYPE(c->ast, pb)->ext)
						pb = AST_TYPE(c->ast, pb)->base;
					pa = AST_QUALIFIED_TYPE_UNQUALIFIED(pa), pb = AST_QUALIFIED_TYPE_UNQUALIFIED(pb);
					if (pa == pb || !cc_type_is_record(c->ast, pa) || !cc_type_is_record(c->ast, pb) || !cc_type_is_record(c->ast, at))
						continue;
					if (cxx_derives(c, pa, pb, 0) && (at == pa || cxx_derives(c, at, pa, 0)))
						better = true;
					else if (cxx_derives(c, pb, pa, 0) && (at == pb || cxx_derives(c, at, pb, 0)))
						worse = true;
				}
			}
			if (!better && !worse && (obj == 1 || obj == 2) && (cand[a].e->fn_const & 3) != (cand[b].e->fn_const & 3) && !cand[a].e->fn_static &&
			    !cand[b].e->fn_static && ((cand[a].e->fn_const & 3) & (cand[b].e->fn_const & 3)) == ((cand[a].e->fn_const & 3) < (cand[b].e->fn_const & 3) ? (cand[a].e->fn_const & 3) : (cand[b].e->fn_const & 3)) &&
			    (obj == 1 || (((cand[a].e->fn_const & 3) & (cand[b].e->fn_const & 3)) & 1))) {
				/* the implicit object argument is one of the conversions ranked: a non-const object binds to
				   the f() better than to the f() const ([over.match.best]/2.1, [over.ics.rank]/3.2.6), before
				   anything about templates */
				better = (cand[a].e->fn_const & 3) < (cand[b].e->fn_const & 3), worse = !better;	/* (and f() const over f() const volatile) */
			}
			if (!better && !worse) {
				if (!cand[a].tmpl && cand[b].tmpl)
					better = true;
				else if (cand[a].tmpl && cand[b].tmpl) {
					/* two templates: the more specialized (fewer parameters that are a bare T) */
					int ga = cxx_ftemplate_bare_params(c, cand[a].e, nargs), gb = cxx_ftemplate_bare_params(c, cand[b].e, nargs);

					if (ga >= 0 && gb >= 0 && ga != gb)
						better = ga < gb, worse = ga > gb;
					/* both all bare and binding to the same types: f(T, T) is more specialized than f(T, U)
					   ([temp.func.order]: U deduces from T, not T from U) */
					else if (ga >= 0 && ga == gb && cxx_ftemplate_distinct_bare(c, cand[a].e, nargs) > 0 &&
						 cxx_ftemplate_distinct_bare(c, cand[b].e, nargs) > 0 &&
						 cxx_ftemplate_distinct_bare(c, cand[a].e, nargs) != cxx_ftemplate_distinct_bare(c, cand[b].e, nargs)) {
						bool same = true;

						for (uint32_t n = 0; n < nargs && n < 16 && same; n++)
							same = cand[a].pt[n] != CXX_TY_UNKNOWN && cand[a].pt[n] == cand[b].pt[n];
						if (same)
							better = cxx_ftemplate_distinct_bare(c, cand[a].e, nargs) < cxx_ftemplate_distinct_bare(c, cand[b].e, nargs),
							worse = !better;
					}
					/* as specialized as each other, one with a requires-clause (satisfied: it is a
					   candidate) and the other without: the constrained one ([temp.func.order]/6.2.2) */
					else if (cxx_ftemplate_constrained(cand[a].e) != cxx_ftemplate_constrained(cand[b].e) &&
						 ga >= 0 && ga == gb && cxx_ftemplate_has_body(cand[a].e) == cxx_ftemplate_has_body(cand[b].e))
						better = cxx_ftemplate_constrained(cand[a].e), worse = !better;
					/* the same parameter types once bound, from fewer template parameters: the more
					   specialized (operator<<(basic_ostream<char, T>&, char) against basic_ostream<C, T>&);
					   before the declaration-or-definition rule below, which is for one template (as many
					   parameters), not two (os << " " otherwise depended on which bodies were seen) */
					else if (cand[a].e->ftmpl && cand[b].e->ftmpl && nargs <= 16 &&
						 cxx_ftemplate_params(cand[a].e->ftmpl)->ntparams != cxx_ftemplate_params(cand[b].e->ftmpl)->ntparams) {
						bool identical = true;

						for (uint32_t n = 0; n < nargs && identical; n++)
							identical = cand[a].pt[n] != CXX_TY_UNKNOWN && cand[a].pt[n] == cand[b].pt[n];
						if (identical)
							better = cxx_ftemplate_params(cand[a].e->ftmpl)->ntparams < cxx_ftemplate_params(cand[b].e->ftmpl)->ntparams, worse = !better;
					}
					/* as specialized as each other: a declaration and its definition are one function, the
					   definition stands for it */
					else if (cxx_ftemplate_has_body(cand[a].e) != cxx_ftemplate_has_body(cand[b].e) && cand[a].r == cand[b].r)
						better = cxx_ftemplate_has_body(cand[a].e), worse = !better;
				}
			}
			wins = better && !worse;
		}
		if (wins)
			best = (int)a;
	}
	if (ncand == 1 && !unknown)
		best = 0;
	/* (a template vcc could not deduce might be better than anything but a non-template that matches exactly;
	   or than one that matches every argument exactly, when each such template's parameter that depends on none
	   of its own needs a conversion: impl(__priority_tag<0>, ...) against impl(__priority_tag<1>, ...)) */
	if (best >= 0 && unknown) {
		bool exact = !cand[best].tmpl && cand[best].ranked;

		for (uint32_t n = 0; n < nargs && exact; n++)
			exact = cand[best].rank[n] <= 1;
		if (!exact && cand[best].ranked && nunk && nunk < 8 && args && nargs <= 16) {
			bool all0 = true, worse = true;

			for (uint32_t n = 0; n < nargs && all0; n++)
				all0 = cand[best].rank[n] == 0;
			for (uint32_t u = 0; u < nunk && all0 && worse; u++) {
				struct cxx_ftemplate *ft = unk[u]->ftmpl;
				bool w = false;

				uint32_t fnp = ft ? cxx_ftemplate_np(c, ft) : 0;

				for (uint32_t n = 0; n < nargs && n < fnp && !w; n++) {
					ast_qualified_type pt = cxx_ftemplate_pt(ft, n);

					if (pt != CXX_TY_UNKNOWN && !cxx_type_has_placeholder(c, pt) && args[n] != CXX_TY_UNKNOWN)
						w = cxx_arg_rank(c, pt, args[n], lval[n], have_lval) > 0;
				}
				worse = w;
			}
			exact = all0 && worse;
		}
		if (!exact)
			best = -1;
	}
	/*
	 * No best one.  Two candidates nothing beats that convert an argument to different types by
	 * conversions of the same rank are ambiguous for sure: os << 1 through operator<<(os, char) and
	 * operator<<(os, signed char) (not where one of them may be a promotion, which vcc does not rank).
	 * Otherwise the candidates' common return type stands, without saying the call is well-formed.
	 */
	bool ambiguous = false;

	if (best < 0 && ncand >= 2 && !unknown && nargs <= 16) {
		for (uint32_t a = 0; a < ncand && !ambiguous; a++)
			for (uint32_t b = a + 1; b < ncand && !ambiguous; b++) {
				bool differ = false, top = cand[a].ranked && cand[b].ranked && cand[a].tmpl == cand[b].tmpl;

				for (uint32_t n = 0; n < nargs && top; n++) {
					ast_qualified_type pa = cand[a].pt[n], pb = cand[b].pt[n];

					top = cand[a].rank[n] == cand[b].rank[n];
					if (top && cand[a].rank[n] == 2 && pa != CXX_TY_UNKNOWN && pb != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(pa) != AST_QUALIFIED_TYPE_UNQUALIFIED(pb)) {
						int ka = AST_TYPE_KIND(c->ast, pa), kb = AST_TYPE_KIND(c->ast, pb);
						bool promo = ka == AST_TYPE_INT || ka == AST_TYPE_UNSIGNED_INT || ka == AST_TYPE_DOUBLE || kb == AST_TYPE_INT || kb == AST_TYPE_UNSIGNED_INT || kb == AST_TYPE_DOUBLE;

						differ |= ka >= AST_TYPE_BOOL && ka <= AST_TYPE_FLOAT128 && kb >= AST_TYPE_BOOL && kb <= AST_TYPE_FLOAT128 && !promo;
					}
				}
				/* (and no other candidate is better than them) */
				for (uint32_t k = 0; k < ncand && top && differ; k++) {
					bool better = false, worse = false;

					if (k == a || k == b)
						continue;
					if (!cand[k].ranked) {
						top = false;
						break;
					}
					for (uint32_t n = 0; n < nargs; n++) {
						better |= cand[k].rank[n] < cand[a].rank[n];
						worse |= cand[k].rank[n] > cand[a].rank[n];
					}
					if (better && !worse)
						top = false;
					if (!better && !worse && !cand[k].tmpl && cand[a].tmpl)
						top = false;	/* (a non-template that is as good wins) */
				}
				ambiguous = top && differ;
			}
		if (ambiguous) {
			same = false;
			*verdict |= 1, CXX_INVALID(c);
		} else if (same && viable >= 2)
			*verdict |= 2;
	}
	c->none_viable = 0;	/* (this call's verdict, not that of the calls made while deducing its candidates) */
	c->call_resolved = best >= 0;
	c->call_picked = best >= 0 ? cand[best].e : NULL;
	c->call_nranks = 0;
	if (best >= 0 && cand[best].ranked && nargs <= 16) {
		memcpy(c->call_ranks, cand[best].rank, nargs);
		c->call_nranks = (uint8_t)(nargs ? nargs : 0);
	}
	if (best >= 0)
		result = cand[best].r;
	else if (!viable || !same || unknown) {
		c->none_viable = !viable && !unknown;
		if (c->none_viable && !operator_call)
			*verdict |= 1, CXX_INVALID(c);	/* no function fits these arguments (a requirement fails) */
		if (!c->template_depth && !(c->none_viable && operator_call))
			c->stats.why[unknown ? c->why : !viable ? CXX_WHY_NONE_VIABLE : CXX_WHY_AMBIGUOUS]++;
		result = CXX_TY_UNKNOWN;
	}
	if (result == CXX_TY_UNKNOWN && (best >= 0 || (viable && same && !unknown)) && !c->template_depth)
		c->stats.why[CXX_WHY_RETURN]++;
	if (!c->template_depth && !(c->none_viable && operator_call))
		c->stats.calls++, c->stats.calls_typed += result != CXX_TY_UNKNOWN;
	if (result == CXX_TY_UNKNOWN && !c->template_depth && !(c->none_viable && operator_call) && getenv("VCC_CXX_WHY"))
		fprintf(stderr, "why@%u %s %.*s/%u\n", c->tok[c->pos ? c->pos - 1 : 0].loc, best >= 0 || (viable && same && !unknown) ? "return" :
			unknown ? "unknown" : !viable ? "none" : "ambiguous", (int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs);
	if (result == CXX_TY_UNKNOWN && !c->template_depth && !c->instantiating && !c->unevaluated && !(c->none_viable && operator_call) &&
	    getenv("VCC_CXX_WHY_TOP")) {
		fprintf(stderr, "top %s %.*s/%u:", best >= 0 || (viable && same && !unknown) ? "return" : unknown ? "unknown" : !viable ? "none" : "ambiguous",
			(int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs);
		for (uint32_t i = c->pos > 10 ? c->pos - 10 : 0; i < c->pos && i < c->ntok; i++)
			fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
		fputc('\n', stderr);
	}
	if (result == CXX_TY_UNKNOWN && !c->template_depth && getenv("VCC_CXX_WHY_TOKENS")) {
		const char *want = getenv("VCC_CXX_WHY_TOKENS");

		if (strlen(want) == vintern_ents[set->name].len && !memcmp(want, vintern_ents[set->name].s, strlen(want))) {
			fprintf(stderr, "tokens:");
			for (uint32_t i = c->pos > 14 ? c->pos - 14 : 0; i < c->pos + 4 && i < c->ntok; i++)
				fprintf(stderr, " %.*s", (int)c->tok[i].len, c->tok[i].s);
			fputc('\n', stderr);
		}
	}
	if (getenv("VCC_CXX_PICK") && !c->template_depth && result != CXX_TY_UNKNOWN)
		fprintf(stderr, "pick %s %s %s %.*s\n", best < 0 ? "shared-type" : cand[best].tmpl ? "template" : cand[best].e->late ? "member-late" : "plain",
			c->unevaluated ? "unevaluated" : "evaluated", best >= 0 && cand[best].e->late && cand[best].e->late_done ? "done" : "-",
			(int)vintern_ents[set->name].len, vintern_ents[set->name].s);
	/* the function called (when known): its body is instantiated, in evaluated code */
	cxx_entity *called_instance = NULL;

	if (best >= 0) {
		cxx_entity *called = cand[best].e;

		if (called->kind == CXX_ENTITY_VALUE)
			cxx_member_body_use(c, called);
		else if (called->kind == CXX_ENTITY_FUNCTION_TEMPLATE && !c->unevaluated && !c->tentative && nargs <= 16) {
			ast_qualified_type r, et = result;

			c->call_instance = NULL;
			cxx_ftemplate_deduce(c, called, xargs, nxargs, args, lval, nargs, &r, true);
			result = et != CXX_TY_UNKNOWN ? et : r;
			called_instance = c->call_instance;	/* (code generation: the function this call calls) */
			if (called_instance && called_instance->inst_ftmpl != called->ftmpl) {
				called_instance = NULL;	/* (an instance an inner call made, not this one's) */
			}
		}
	}
	(void)first;
	/* (this call's verdict again: the bodies instantiated above made calls of their own) */
	c->call_resolved = best >= 0;
	c->call_picked = best >= 0 ? (called_instance ? called_instance : cand[best].e) : NULL;
	c->none_viable = best < 0 && !viable && !unknown;
	c->call_nranks = 0;	/* (and its picked candidate's ranks, cxx_operator_call_type compares them) */
	if (best >= 0 && cand[best].ranked && nargs <= 16) {
		memcpy(c->call_ranks, cand[best].rank, nargs);
		c->call_nranks = (uint8_t)nargs;
	}
	cxx_et(c, result, false);
}


/*
 * An operator with a class operand: obj's member operator (args: the other operands), else
 * a namespace-scope one found by ordinary lookup (args: all operands; ADL not yet).  false if
 * neither is found: the built-in operator applies.
 */
/* the friend functions named id that the class of t (or a base of it) declares */
static cxx_entity *cxx_friend_lookup_in(cxx *c, cxx_scope *cls, uint32_t id, int depth)
{
	cxx_entity *e;

	if (!cls || depth > 6)
		return NULL;
	if (cls->friends && (e = cxx_lookup_in(c, cls->friends, id, 0)) && e->kind != CXX_ENTITY_DEPENDENT)
		return e;
	for (uint32_t i = 0; i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS && (e = cxx_friend_lookup_in(c, cls->used[i], id, depth + 1)))
			return e;
	return NULL;
}

static cxx_entity *cxx_friend_lookup(cxx *c, ast_qualified_type t, uint32_t id, int depth)
{
	if (t == CXX_TY_UNKNOWN)
		return NULL;
	if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
		t = AST_TYPE(c->ast, t)->base;
	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY)
		return NULL;
	return cxx_friend_lookup_in(c, cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)), id, depth);
}

VPRIV bool cxx_operator_call_type(cxx *c, uint32_t id, ast_qualified_type obj, const ast_qualified_type *args, uint32_t nargs)
{
	cxx_scope *cls;
	cxx_entity *set;
	ast_qualified_type all[4], acopy[16];
	bool have_lv = c->op_lv_set && nargs <= 16;
	uint8_t lv[17];
	bool any_class = false, any_enum = false;

	/* The member, namespace-scope and friend operator functions are one candidate set in C++; here they
	   are tried in turn.  An attempt that picks a function decides; one that is only not ruled out (several
	   candidates vcc cannot order, os << 'c' among ostream's members) waits for the later ones (std's
	   operator<<(ostream&, char) is exact) and stands, unsure, if none of them decides. */
	uint8_t unsure0 = c->eunsure;
	bool pending = false, pending_lval = false;
	ast_qualified_type pending_type = CXX_TY_UNKNOWN;
	/* a member that needs a conversion where a non-member may not (os << " ": operator<<(const void *) and
	   operator<<(ostream&, const char *)): set aside while the non-members are tried, kept unless one binds
	   the object exactly and converts the operand better */
	bool mpick = false, m_lval = false;
	ast_qualified_type m_type = CXX_TY_UNKNOWN;
	cxx_entity *m_picked = NULL, *m_callee = NULL;
	uint8_t m_obj = 0, m_unsure = 0, m_rank = 0, m_const = 0;
	int64_t m_value = 0;
#define OP_RESTORE_MEMBER() (cxx_et(c, m_type, m_lval), c->call_resolved = 1, c->call_picked = m_picked, c->call_obj = m_obj, \
			      c->eunsure = m_unsure, c->ecallee = m_callee, c->econst = m_const, c->evalue = m_value, c->none_viable = 0, true)
#define OP_ATTEMPT_DONE() do { \
		if (c->call_resolved && mpick) { \
			if (c->call_nranks == 2 && c->call_ranks[0] <= 1 && c->call_ranks[1] < m_rank) \
				return true; \
			return OP_RESTORE_MEMBER(); \
		} \
		if (c->call_resolved) \
			return true; \
		if (!c->none_viable && !pending) \
			pending = true, pending_type = c->etype, pending_lval = c->elval; \
	} while (0)

	memcpy(lv, c->op_lv, sizeof lv);
	c->op_lv_set = 0;
	bool op_nl = c->op_no_literal;	/* (operands that are no constant 0: an integer is no null pointer constant) */

	c->op_no_literal = 0;
	c->call_resolved = 0, c->call_picked = NULL;	/* (what an earlier call picked is not this operator's function) */
	if (id <= 1 || obj == CXX_TY_UNKNOWN || nargs > 16)
		return false;
	memcpy(acopy, args, nargs * sizeof *args);	/* (often &c->etype, which the first lookup's call resets) */
	args = acopy;
	cls = cc_type_is_record(c->ast, obj) && AST_TYPE_KIND(c->ast, obj) != AST_TYPE_ARRAY ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(obj)) : NULL;
	/* (1 + x, "a" + s: the left operand is no class; the operator functions of the right one's) */
	for (uint32_t i = 0; i < nargs && nargs <= 2; i++)
		any_class |= args[i] != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, args[i]) && AST_TYPE_KIND(c->ast, args[i]) != AST_TYPE_ARRAY &&
			     cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(args[i]));
	/* (an enumeration operand: its non-member operator functions too, [over.match.oper]/3.2 -- json's
	   operator<=>(value_t, value_t); with none found, the built-in operator) */
	for (uint32_t i = 0; !cls && !any_class && i <= nargs && nargs <= 2; i++) {
		ast_qualified_type t = i ? args[i - 1] : obj;

		any_enum |= t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_ENUM;
	}
	if (!cls && !any_class && !any_enum)
		return false;
	/* (no candidate fits: the next place, in the end the built-in operator: &x, a = b, a, b) */
	set = cls ? cxx_lookup_in(c, cls, id, 0) : NULL;
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		c->call_obj = (AST_QUALIFIED_TYPE_QUALIFIERS(obj) & AST_QUALIFIER_CONST) ? 2 : 1;	/* a member operator: on this object, const or not */
		c->call_obj_rv = have_lv ? (lv[0] ? 2 : 1) : 0;
		c->no_literal = op_nl, c->eunsure = unsure0, cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, have_lv ? lv + 1 : NULL, nargs, true);
		if (c->call_resolved && nargs == 1 && c->call_nranks == 1 && c->call_ranks[0] >= 2 && !c->tentative) {
			mpick = true, m_type = c->etype, m_lval = c->elval, m_picked = c->call_picked, m_obj = c->call_obj;
			m_unsure = c->eunsure, m_callee = c->ecallee, m_const = c->econst, m_value = c->evalue, m_rank = c->call_ranks[0];
			c->call_resolved = 0;
		} else
			OP_ATTEMPT_DONE();
		/* obj(args) and no operator() of its class takes them: ill-formed (a conversion to a pointer
		   to function aside) */
		if (vintern_ents[id].len == 10 && !memcmp(vintern_ents[id].s, "operator()", 10) && !c->tentative && !mpick) {	/* (mpick: one fits, set aside) */
			bool known = true;

			for (uint32_t i = 0; i < nargs; i++)
				known &= args[i] != CXX_TY_UNKNOWN && !cxx_type_has_placeholder(c, args[i]);
			if (known)
				CXX_INVALID(c);
		}
	}
	set = nargs <= 2 ? cxx_lookup(c, id) : NULL;	/* (operator() and operator[] are members) */
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		all[0] = obj;
		for (uint32_t i = 0; i < nargs; i++)
			all[i + 1] = args[i];
		c->no_literal = op_nl, c->eunsure = unsure0, cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, all, have_lv ? lv : NULL, nargs + 1, true);
		OP_ATTEMPT_DONE();
	}
	/* the operator functions in the namespaces of the operands' classes (argument-dependent lookup):
	   std's operator== for two std::string, operator+ for a string and a char */
	if (nargs <= 2 && !c->tentative) {
		cxx_scope *seen[16];
		uint32_t nseen = 0, nq = 0;
		ast_qualified_type q[24];

		all[0] = obj;
		for (uint32_t i = 0; i < nargs; i++)
			all[i + 1] = args[i];
		/* the operands' classes and their base classes ([basic.lookup.argdep]/3: a class derived from
		   std::reverse_iterator finds std's operator==) */
		for (uint32_t i = 0; i <= nargs; i++) {
			ast_qualified_type t = all[i];

			if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
				t = AST_TYPE(c->ast, t)->base;
			if (t != CXX_TY_UNKNOWN && nq < 24)
				q[nq++] = t;
		}
		for (uint32_t i = 0; i < nq && nq < 24; i++)
			if (q[i] != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, q[i]) && AST_TYPE_KIND(c->ast, q[i]) != AST_TYPE_ARRAY && i < 12) {
				ast_qualified_type b[8];
				uint32_t nb = cxx_class_bases(c, q[i], b, 8);

				for (uint32_t k = 0; k < nb && nq < 24; k++)
					q[nq++] = b[k];
			}
		for (uint32_t i = 0; i < nq; i++) {
			ast_qualified_type t = q[i];
			cxx_scope *ns;
			bool dup = false;

			if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY ||
			    !(ns = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t))))
				continue;
			while (ns && ns->kind != CXX_SCOPE_NAMESPACE)
				ns = ns->parent;
			for (uint32_t k = 0; k < nseen; k++)
				dup |= seen[k] == ns;
			if (!ns || dup || nseen == 16)
				continue;
			seen[nseen++] = ns;
			set = cxx_lookup_in(c, ns, id, 0);
			if (!set || set->kind == CXX_ENTITY_DEPENDENT)
				continue;
			c->no_literal = op_nl, c->eunsure = unsure0, cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, all, have_lv ? lv : NULL, nargs + 1, true);
			OP_ATTEMPT_DONE();
		}
		/* ... and the friends the operands' classes declare (hidden friends: found this way only) */
		for (uint32_t i = 0; i <= nargs; i++) {
			set = cxx_friend_lookup(c, all[i], id, 0);
			if (!set)
				continue;
			c->no_literal = op_nl, c->eunsure = unsure0, cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, all, have_lv ? lv : NULL, nargs + 1, true);
			OP_ATTEMPT_DONE();
		}
	}
	if (mpick)
		return OP_RESTORE_MEMBER();
	if (any_enum && !cls && !any_class) {
		/* enumerations only, no operator function picked: the built-in operator (t == value_t::null with
		   json's operator== templates in scope) */
		c->call_resolved = 0, c->call_picked = NULL, c->none_viable = 0;
		return false;
	}
	if (pending) {
		cxx_et(c, pending_type, pending_lval);
		c->eunsure = 1;
		return true;
	}
	c->eunsure = unsure0;
	/* built-in: the address of an object; anything else on a class is not known (an implicit operator=,
	   a conversion to a built-in type) */
	if (vintern_ents[id].len == 9 && !memcmp(vintern_ents[id].s, "operator&", 9) && nargs == 0)
		return false;
	/* No operator function takes the operands.  The built-in operator is left, for operands their classes
	   convert to something it takes: with no conversion function in them the expression is ill-formed
	   (os << x for a class with no operator<<).  Assignment and the comma have implicit meanings. */
	{
		const vident *v = &vintern_ents[id];
		bool implicit = (v->len == 9 && (v->s[8] == '=' || v->s[8] == ',')) || (v->len == 10 && !memcmp(v->s + 8, "->", 2));
		/* (a comparison: C++20's rewritten candidates, a <=> b and the reversed b == a, are tried after this; the
		   operator's caller decides it is ill-formed once they fail too -- decltype(l <= r) of libc++'s __wrap_iter) */
		bool cmp = (v->len == 9 && (v->s[8] == '<' || v->s[8] == '>')) ||
			   (v->len == 10 && (!memcmp(v->s + 8, "<=", 2) || !memcmp(v->s + 8, ">=", 2) || !memcmp(v->s + 8, "==", 2) || !memcmp(v->s + 8, "!=", 2)));
		bool known = !implicit && !cmp && !c->tentative && nargs <= 2, stuck = false;

		all[0] = obj;
		for (uint32_t i = 0; i < nargs && nargs <= 2; i++)
			all[i + 1] = args[i];
		/* (one class operand that converts to nothing is enough: the built-in operator takes no class) */
		for (uint32_t i = 0; known && i <= nargs; i++) {
			ast_qualified_type t = all[i];
			cxx_scope *os;

			if (t == CXX_TY_UNKNOWN || cxx_type_has_placeholder(c, t)) {
				known = false;
				break;
			}
			if (!cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY)
				continue;
			os = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
			stuck |= os && !cxx_class_converts(c, os, 0);
		}
		if (known && stuck)
			CXX_INVALID(c);
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
	return true;
}
#undef OP_ATTEMPT_DONE

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
VPRIV bool cxx_is_initializer_list(cxx *c, ast_qualified_type t)
{
	cxx_scope *s;

	if (t == CXX_TY_UNKNOWN)
		return false;
	if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
		t = AST_TYPE(c->ast, t)->base;
	if (!cc_type_is_record(c->ast, t) || !(s = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t))) || !s->owner)
		return false;
	return vintern_ents[s->owner->name].len == 16 && !memcmp(vintern_ents[s->owner->name].s, "initializer_list", 16);
}

/*
 * An object of class type t is initialized from args (braced: a braced-init-list): the constructor
 * chosen (an initializer_list one first for braces), then the destructor; their bodies are
 * instantiated when t is an instance.  The expression's type is left as it was.
 */
/* the elements of the braced list at '{' (pos): how many at its top level */
static uint32_t cxx_brace_count(cxx *c, uint32_t pos)
{
	uint32_t n = 0, depth = 0;
	bool any = false;

	for (uint32_t i = pos + 1; i < c->ntok; i++) {
		int k = c->tok[i].kind;

		if (k == LEX_TOKEN_LEFT_BRACE || k == LEX_TOKEN_LEFT_PAREN || k == LEX_TOKEN_LEFT_SQUARE)
			depth++;
		else if ((k == LEX_TOKEN_RIGHT_BRACE || k == LEX_TOKEN_RIGHT_PAREN || k == LEX_TOKEN_RIGHT_SQUARE) && depth)
			depth--;
		else if (k == LEX_TOKEN_RIGHT_BRACE)
			return any ? n + 1 : 0;
		else if (k == LEX_TOKEN_COMMA && !depth)
			n++;
		any = true;
	}
	return UINT32_MAX;
}

/* can std::initializer_list<E> (il) take the braced list's elements: an element that is a braced list of more
   than one element is no scalar E's ([dcl.init.list]/3.10: vector<uint8_t>{{1, 2, 3}} is a copy of the vector
   the inner list makes, not a list of one byte) */
static bool cxx_ilist_elements_fit(cxx *c, ast_qualified_type il, const ast_qualified_type *args, const uint8_t *lval, uint32_t nargs)
{
	cxx_scope *s;
	cxx_entity *vt;
	ast_qualified_type e;

	if (AST_TYPE_KIND(c->ast, il) == AST_TYPE_POINTER && AST_TYPE(c->ast, il)->ext)
		il = AST_TYPE(c->ast, il)->base;
	/* (E: its value_type) */
	if (!(s = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(il))) || !(vt = cxx_lookup_local(s, vintern_cstr("value_type"))) ||
	    vt->kind != CXX_ENTITY_TYPE || (e = vt->ty) == CXX_TY_UNKNOWN)
		return true;
	if (cc_type_is_record(c->ast, e)) {
		/* a class element: an argument that does not convert to it at all rules the list constructor out
		   (vector<pair<const K, V>>{first, last, alloc} in json's ordered_map: the iterator-range one) */
		ast_qualified_type et = c->etype;
		bool el = c->elval;
		bool fits = true;
		uint32_t n = nargs < CXX_MAX_ARGS ? nargs : CXX_MAX_ARGS;
		/* (the ranking resolves calls of its own, which reuse the argument records: the list's are kept) */
		uint32_t *br = malloc(sizeof c->carg_brace), ncargs = c->ncargs;

		if (!br)
			return true;
		memcpy(br, c->carg_brace, sizeof c->carg_brace);
		for (uint32_t i = 0; i < n && fits; i++)
			if (!(br[i] && br[i] < c->ntok && c->tok[br[i]].kind == LEX_TOKEN_LEFT_BRACE) &&
			    args && args[i] != CXX_TY_UNKNOWN && !cxx_type_has_placeholder(c, args[i]))
				fits = cxx_arg_rank(c, AST_QUALIFIED_TYPE_UNQUALIFIED(e), args[i], lval ? lval[i] != 0 : false, lval != NULL) < 6;
		memcpy(c->carg_brace, br, sizeof c->carg_brace);
		free(br);
		c->ncargs = ncargs;
		c->etype = et, c->elval = el;
		return fits;
	}
	for (uint32_t i = 0; i < nargs && i < CXX_MAX_ARGS; i++)
		if (c->carg_brace[i] && c->carg_brace[i] < c->ntok && c->tok[c->carg_brace[i]].kind == LEX_TOKEN_LEFT_BRACE) {
			uint32_t k = cxx_brace_count(c, c->carg_brace[i]);

			if (k != UINT32_MAX && k > 1)
				return false;
		} else if (args && args[i] != CXX_TY_UNKNOWN && cc_type_is_arithmetic(c->ast, e)) {
			/* (string{"~"}: a char array or pointer is no char, the const char * constructor is the one) */
			int k = AST_TYPE_KIND(c->ast, args[i]);

			if (k == AST_TYPE_ARRAY || k == AST_TYPE_POINTER || k == AST_TYPE_FUNCTION)
				return false;
		}
	return true;
}

VPRIV void cxx_ctor_use(cxx *c, ast_qualified_type t, const ast_qualified_type *args, const uint8_t *lval, uint32_t nargs, bool braced)
{
	ast_qualified_type et = c->etype;
	bool el = c->elval;
	cxx_entity *callee = c->ecallee, *set, *dtor;
	cxx_scope *cls;

	ast_qualified_type acopy[CXX_MAX_ARGS];
	uint8_t lcopy[CXX_MAX_ARGS];
	uint32_t bcopy[CXX_MAX_ARGS];

	c->ctor_picked = NULL, c->ctor_found = 0, c->ctor_none = 0, c->ctor_ilist = NULL;
	if (c->tentative || nargs == UINT32_MAX || nargs > CXX_MAX_ARGS || t == CXX_TY_UNKNOWN)
		return;
	/* (completing the class parses its members' declarations, their argument lists into c->cargs, which args
	   may be: the first T(a, b) of an instance in a TU saw another call's arguments) */
	if (nargs) {
		if (args)
			memcpy(acopy, args, nargs * sizeof *args), args = acopy;
		if (lval)
			memcpy(lcopy, lval, nargs), lval = lcopy;
	}
	memcpy(bcopy, c->carg_brace, sizeof bcopy);
	if (!(cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)))) {
		memcpy(c->carg_brace, bcopy, sizeof bcopy);
		return;
	}
	cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	memcpy(c->carg_brace, bcopy, sizeof bcopy);
	set = cxx_lookup_in(c, cls, cxx_ctor_id(), 0);
	c->ctor_none = !set;
	if (set && braced && nargs) {
		/* T{a, b}: a constructor whose first parameter is std::initializer_list (the rest defaulted); T{}:
		   value-initialization, the default constructor ([dcl.init.list] 3.5) */
		for (cxx_entity *e = set; e; e = e->next) {
			const ctype *ft;

			if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN)
				continue;
			ft = AST_TYPE(c->ast, e->ty);
			if (ft->n >= 1 && ft->n <= 1u + e->ndefaults && cxx_is_initializer_list(c, c->ast->params[ft->aux]) &&
			    cxx_ilist_elements_fit(c, c->ast->params[ft->aux], args, lval, nargs)) {
				/* (the one with fewest parameters: vector(initializer_list) before (initializer_list, allocator)) */
				if (!c->ctor_ilist || ft->n < AST_TYPE(c->ast, c->ctor_ilist->ty)->n)
					c->ctor_ilist = e;
			}
		}
		if (c->ctor_ilist) {
			cxx_entity *il = c->ctor_ilist;
			uint32_t br[CXX_MAX_ARGS];

			ast_qualified_type ty[CXX_MAX_ARGS];
			uint8_t lv[CXX_MAX_ARGS];
			uint32_t n = nargs < CXX_MAX_ARGS ? nargs : CXX_MAX_ARGS;

			/* (the list just parsed, before the body's instantiation reuses c->cargs, which args may be) */
			memcpy(br, c->carg_brace, sizeof br);
			for (uint32_t i = 0; i < n; i++)
				ty[i] = args ? args[i] : CXX_TY_UNKNOWN, lv[i] = lval ? lval[i] : 0;
			cxx_member_body_use(c, il);	/* (its body constructs things of its own) */
			memcpy(c->ctor_ilist_brace, br, sizeof br);
			memcpy(c->ctor_ilist_types, ty, n * sizeof *ty), memcpy(c->ctor_ilist_lval, lv, n);
			c->ctor_ilist = il, c->ctor_ilist_n = n;
			set = NULL;
		}
	}
	/* (from an object of the same class with no constructor that fits: the implicit copy or move) */
	c->call_obj = 0;	/* (a constructor has no object: what a member access in the arguments left) */
	if (set) {
		cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, lval, nargs,
				   nargs == 1 && args[0] != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(args[0]) == AST_QUALIFIED_TYPE_UNQUALIFIED(t));
		c->ctor_found = c->call_resolved && c->call_picked && c->call_picked->kind == CXX_ENTITY_VALUE;
		c->ctor_picked = c->ctor_found ? c->call_picked : NULL;
	}
	dtor = cxx_lookup_in(c, cls, cxx_dtor_id(), 0);
	if (dtor && dtor->kind == CXX_ENTITY_VALUE) {
		/* (the destructor's body, instantiated here, constructs things of its own: keep this choice) */
		cxx_entity *picked = c->ctor_picked, *il = c->ctor_ilist;
		uint8_t found = c->ctor_found, none = c->ctor_none;

		cxx_member_body_use(c, dtor);
		c->ctor_picked = picked, c->ctor_found = found, c->ctor_none = none, c->ctor_ilist = il;
	}
	c->etype = et, c->elval = el, c->ecallee = callee;
}

/* does the class have a conversion function to a type a built-in operator takes (arithmetic, an enumeration,
   a pointer; a conversion template: perhaps): std::string's to string_view does not count */
static bool cxx_class_converts_scalar(cxx *c, cxx_scope *cls, int depth)
{
	if (!cls || cls->has_dependent_base || depth > 12)
		return true;
	if (cls->has_conversion)
		for (uint32_t i = 0; i < cls->nslots; i++)
			for (cxx_entity *e = cls->slots[i]; e; e = e->next) {
				const vident *v = &vintern_ents[e->name];
				ast_qualified_type t;

				if (v->len < 10 || memcmp(v->s, "operator ", 9))
					continue;
				if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE)
					return true;
				if (v->s[9] != '#')
					continue;
				t = (ast_qualified_type)strtoul(v->s + 10, NULL, 10);
				if (t == CXX_TY_UNKNOWN || t == 0)
					return true;
				if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
					t = AST_TYPE(c->ast, t)->base;
				int k = AST_TYPE_KIND(c->ast, t);

				if ((k >= AST_TYPE_BOOL && k <= AST_TYPE_FLOAT128) || k == AST_TYPE_ENUM || k == AST_TYPE_POINTER)
					return true;
			}
	for (uint32_t i = 0; i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS && cxx_class_converts_scalar(c, cls->used[i], depth + 1))
			return true;
	return false;
}

VPRIV bool cxx_class_converts_public(cxx *c, cxx_scope *cls)
{
	cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(cls->owner ? cls->owner->ty : CXX_TY_UNKNOWN));
	return cxx_class_converts_scalar(c, cls, 0);
}

/* ---- integer constant expressions ---------------------------------------------------------- */

/* an expression of type t with the constant value v */
VPRIV void cxx_ec(cxx *c, ast_qualified_type t, int64_t v)
{
	cxx_et(c, t, false);
	if (t != CXX_TY_UNKNOWN) {
		int k = AST_TYPE_KIND(c->ast, t);
		uint64_t size = AST_TYPE(c->ast, t)->size;

		/* the value in t: bool is 0 or 1, a narrower integer wraps (sign- or zero-extended) */
		if (k == AST_TYPE_BOOL)
			v = v != 0;
		else if (k >= AST_TYPE_CHAR && k <= AST_TYPE_UNSIGNED_LONG_LONG && size && size < 8) {
			int sh = (int)(64 - size * 8);

			v = cc_type_is_signed(c->ast, t) ? (int64_t)((uint64_t)v << sh) >> sh : (int64_t)(((uint64_t)v << sh) >> sh);
		}
	}
	c->evalue = v, c->econst = 1;
	if (c->gen)	/* (code generation: the constant's node) */
		cxx_gen_integer(c, v, t, c->tok[c->pos ? c->pos - 1 : 0].loc);
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

static int cxx_class_constructs(cxx *c, ast_qualified_type to, const ast_qualified_type *args, const uint8_t *lval, uint32_t n, bool implicit);
static bool cxx_class_converts(cxx *c, cxx_scope *cls, int depth);

/*
 * clang's type trait builtins that depend on the type's form only (those that ask what a class can
 * do, __is_constructible and the like, are not known).  t: the type arguments.
 */
/* does class t declare a user-provided (not defaulted, not deleted) member named id taking one t (by value or
   by reference: a copy or move constructor, assignment), which makes the operation not trivial
   ([class.copy.ctor]/11, [class.copy.assign]/9) */
static bool cxx_class_user_special(cxx *c, ast_qualified_type t, uint32_t id)
{
	cxx_scope *cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));

	if (!cls)
		return false;
	for (cxx_entity *e = cxx_lookup_local(cls, id); e; e = e->next) {
		const ctype *ft;
		ast_qualified_type p0;

		if (e->kind != CXX_ENTITY_VALUE || e->fn_defaulted || e->fn_deleted || e->ty == CXX_TY_UNKNOWN ||
		    AST_TYPE_KIND(c->ast, e->ty) != AST_TYPE_FUNCTION)
			continue;
		ft = AST_TYPE(c->ast, e->ty);
		if (ft->n < 1 || ft->n > 1u + e->ndefaults)
			continue;
		p0 = c->ast->params[ft->aux];
		if (p0 != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, p0) == AST_TYPE_POINTER && AST_TYPE(c->ast, p0)->ext)
			p0 = AST_TYPE(c->ast, p0)->base;
		if (p0 != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(p0) == AST_QUALIFIED_TYPE_UNQUALIFIED(t))
			return true;
	}
	return false;
}

VPRIV bool cxx_trait_value(cxx *c, const char *n, size_t len, const ast_qualified_type *t, uint32_t nt, int64_t *v)
{
	const ctype *x;
	int k;
	bool ref;
#define IS(s) (len == sizeof s - 1 && !memcmp(n, s, len))

	if (nt == 2 && (IS("__reference_constructs_from_temporary") || IS("__reference_converts_from_temporary"))) {
		/* would T (a reference) bound to an expression of type U make a temporary: its referent not U's type nor a
		   base of it (a conversion's result); not a reference: false */
		ast_qualified_type r = t[0], u = t[1];

		if (r == CXX_TY_UNKNOWN || u == CXX_TY_UNKNOWN)
			return false;
		if (AST_TYPE_KIND(c->ast, r) != AST_TYPE_POINTER || !AST_TYPE(c->ast, r)->ext) {
			*v = 0;
			return true;
		}
		r = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c->ast, r)->base);
		if (AST_TYPE_KIND(c->ast, u) == AST_TYPE_POINTER && AST_TYPE(c->ast, u)->ext)
			u = AST_TYPE(c->ast, u)->base;
		u = AST_QUALIFIED_TYPE_UNQUALIFIED(u);
		if (r == u || (cc_type_is_record(c->ast, r) && cc_type_is_record(c->ast, u) && cxx_derives(c, u, r, 0))) {
			*v = 0;
			return true;
		}
		if (cc_type_is_record(c->ast, r) || cc_type_is_record(c->ast, u))
			return false;	/* (a class conversion: whether it binds directly is not decided here) */
		*v = 1;	/* (scalars of another type: a converted temporary) */
		return true;
	}
	if (nt == 2 && IS("__is_same")) {
		*v = t[0] == t[1];
		return true;
	}
	/* is a < b (a: const L&, b: const R&) a rewritten candidate, (a <=> b) < 0, or 0 < (b <=> a), when
	   no operator< fits (clang's builtin; libc++'s __default_three_way_comparator): as the binary
	   operator decides it (expression/binary.c) */
	if (nt == 2 && IS("__builtin_lt_synthesizes_from_spaceship")) {
		ast_qualified_type l = t[0], r = t[1];
		ast_qualified_type et = c->etype;
		uint8_t elval = c->elval, econst = c->econst, resolved = c->call_resolved, nv = c->none_viable;
		int64_t ev = c->evalue;
		cxx_entity *picked = c->call_picked;
		bool lt, ss = false;

		if (l != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, l) == AST_TYPE_POINTER && AST_TYPE(c->ast, l)->ext)
			l = AST_TYPE(c->ast, l)->base;
		if (r != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, r) == AST_TYPE_POINTER && AST_TYPE(c->ast, r)->ext)
			r = AST_TYPE(c->ast, r)->base;
		if (l == CXX_TY_UNKNOWN || r == CXX_TY_UNKNOWN || c->tentative)
			return false;
		if (!(cc_type_is_record(c->ast, l) && AST_TYPE_KIND(c->ast, l) != AST_TYPE_ARRAY) &&
		    !(cc_type_is_record(c->ast, r) && AST_TYPE_KIND(c->ast, r) != AST_TYPE_ARRAY)) {
			*v = 0;	/* (no class: the built-in <) */
			return true;
		}
		c->op_lv_set = 1, c->op_lv[0] = 1, c->op_lv[1] = 1;
		lt = cxx_operator_call_type(c, vintern_cstr("operator<"), l, &r, 1) && c->call_resolved;
		if (!lt) {
			ast_qualified_type ra = r, la = l;

			c->op_lv_set = 1, c->op_lv[0] = 1, c->op_lv[1] = 1;
			ss = cxx_operator_call_type(c, vintern_cstr("operator<=>"), l, &ra, 1) && c->call_resolved;
			if (!ss) {
				c->op_lv_set = 1, c->op_lv[0] = 1, c->op_lv[1] = 1;
				ss = cxx_operator_call_type(c, vintern_cstr("operator<=>"), r, &la, 1) && c->call_resolved;
			}
		}
		c->etype = et, c->elval = elval, c->econst = econst, c->evalue = ev, c->call_resolved = resolved, c->none_viable = nv,
		c->call_picked = picked;
		*v = !lt && ss;
		return true;
	}
	/* a class from or to an object of its own type: its copy or move constructor, or assignment operator */
	if (nt == 2 && (IS("__is_trivially_assignable") || IS("__is_nothrow_assignable"))) {
		/* T& = U trivially (or without throwing): scalars of compatible kinds yes; the same class: trivially
		   copyable as vcc laid it out, or no if its copy, move or destructor is not trivial (else not known) */
		ast_qualified_type l = t[0], r = t[1];
		bool nothrow = IS("__is_nothrow_assignable");

		if (AST_TYPE_KIND(c->ast, l) != AST_TYPE_POINTER || !AST_TYPE(c->ast, l)->ext)
			return false;	/* (to a prvalue: classes only, not decided here) */
		l = AST_QUALIFIED_TYPE_UNQUALIFIED(AST_TYPE(c->ast, l)->base);
		if (AST_TYPE_KIND(c->ast, r) == AST_TYPE_POINTER && AST_TYPE(c->ast, r)->ext)
			r = AST_TYPE(c->ast, r)->base;
		r = AST_QUALIFIED_TYPE_UNQUALIFIED(r);
		int lk = AST_TYPE_KIND(c->ast, l), rk = AST_TYPE_KIND(c->ast, r);
		bool larith = lk >= AST_TYPE_BOOL && lk <= AST_TYPE_FLOAT128, rarith = rk >= AST_TYPE_BOOL && rk <= AST_TYPE_FLOAT128;

		if (larith || lk == AST_TYPE_ENUM || (lk == AST_TYPE_POINTER && !AST_TYPE(c->ast, l)->ext)) {
			if ((larith && (rarith || rk == AST_TYPE_ENUM)) || l == r) {
				*v = !(AST_QUALIFIED_TYPE_QUALIFIERS(AST_TYPE(c->ast, t[0])->base) & AST_QUALIFIER_CONST) && (lk != AST_TYPE_ENUM || l == r);
				return true;
			}
			return false;
		}
		if (cc_type_is_record(c->ast, l) && lk != AST_TYPE_ARRAY && l == r) {
			bool pod, empty;

			if (!nothrow && cxx_class_user_special(c, l, vintern_cstr("operator="))) {
				*v = 0;	/* (a user-provided copy or move assignment: not trivial) */
				return true;
			}
			if (cxx_class_layout_info(c, l, &pod, &empty) && pod) {
				*v = 1;
				return true;
			}
			if (nothrow && cxx_class_assign_trivial(c, l, 0) == 1) {
				*v = 1;	/* (trivial assignments, a union of scalars with constructors of its own: json_value) */
				return true;
			}
			if (!nothrow && cxx_gen_trivial_abi(c, l, 0) == 0) {
				*v = 0;
				return true;
			}
			if (nothrow && !c->tentative) {
				/* (its operator= for that argument: whether it is declared noexcept, when that is known --
				   json's operator=(basic_json) noexcept(...)) */
				ast_qualified_type at[1] = { r };
				ast_qualified_type et = c->etype;
				bool el = c->elval;
				int rv = -1;

				c->op_lv_set = 1, c->op_lv[0] = 1, c->op_lv[1] = !(AST_TYPE_KIND(c->ast, t[1]) == AST_TYPE_POINTER && AST_TYPE(c->ast, t[1])->ext == 2) &&
										     AST_TYPE_KIND(c->ast, t[1]) == AST_TYPE_POINTER;
				bool rlv = c->op_lv[1];

				if (cxx_operator_call_type(c, vintern_cstr("operator="), l, at, 1) && c->call_resolved && c->call_picked &&
				    c->call_picked->fn_noexcept != 2 && c->call_picked->ty != CXX_TY_UNKNOWN && AST_TYPE(c->ast, c->call_picked->ty)->n == 1) {
					cxx_entity *op = c->call_picked;
					ast_qualified_type pt = c->ast->params[AST_TYPE(c->ast, op->ty)->aux];

					rv = op->fn_noexcept == 1;
					if (rv && cc_type_is_record(c->ast, pt) && !(AST_TYPE_KIND(c->ast, pt) == AST_TYPE_POINTER && AST_TYPE(c->ast, pt)->ext)) {
						/* (a parameter by value: also its construction from the argument, the copy or move) */
						uint8_t lv1[1] = { rlv };

						cxx_ctor_use(c, AST_QUALIFIED_TYPE_UNQUALIFIED(pt), at, lv1, 1, false);
						rv = c->ctor_found && c->ctor_picked && c->ctor_picked->fn_noexcept != 2 ? c->ctor_picked->fn_noexcept == 1 : -1;
					}
				}
				c->op_lv_set = 0;
				cxx_et(c, et, el);
				if (rv >= 0) {
					*v = rv;
					return true;
				}
			}
		}
		return false;
	}
	if (nt == 2 && (IS("__is_constructible") || IS("__is_assignable"))) {
		bool assign = IS("__is_assignable");
		ast_qualified_type to = t[0], from = t[1];
		const ctype *tt = AST_TYPE(c->ast, to), *ft = AST_TYPE(c->ast, from);
		bool rvalue = !(ft->kind == AST_TYPE_POINTER && ft->ext == 1);

		if (ft->kind == AST_TYPE_POINTER && ft->ext)
			from = ft->base;
		if (assign) {
			if (!(tt->kind == AST_TYPE_POINTER && tt->ext) || (AST_QUALIFIED_TYPE_QUALIFIERS(tt->base) & AST_QUALIFIER_CONST)) {
				if (!(tt->kind == AST_TYPE_POINTER && tt->ext) && !cc_type_is_record(c->ast, to)) {
					*v = 0;	/* (a scalar rvalue is not assignable) */
					return true;
				}
				if (tt->kind == AST_TYPE_POINTER && tt->ext && !cc_type_is_record(c->ast, tt->base)) {
					*v = 0;	/* (a const scalar) */
					return true;
				}
			}
			if (tt->kind == AST_TYPE_POINTER && tt->ext)
				to = tt->base;
			if (!cc_type_is_record(c->ast, to) && !(AST_QUALIFIED_TYPE_QUALIFIERS(to) & AST_QUALIFIER_CONST)) {
				int rank = cxx_arg_rank0(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), from, !rvalue, true);

				if (rank == 2)
					return false;
				*v = rank < 3 || rank == 5;
				return true;
			}
		}
		if (cc_type_is_record(c->ast, to) && !(AST_TYPE_KIND(c->ast, to) == AST_TYPE_POINTER) && AST_QUALIFIED_TYPE_UNQUALIFIED(to) == AST_QUALIFIED_TYPE_UNQUALIFIED(from) &&
		    !(assign && (AST_QUALIFIED_TYPE_QUALIFIERS(to) & AST_QUALIFIER_CONST))) {
			int r = cxx_class_special(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), assign, rvalue && !(AST_QUALIFIED_TYPE_QUALIFIERS(from) & AST_QUALIFIER_CONST), 0);

			if (r < 0)
				return false;
			*v = r;
			return true;
		}
		/* a class object from something else: its operator= functions, then the implicit ones, which take
		   an object of the class (what the right side converts to, or not) */
		if (assign && cc_type_is_record(c->ast, to) && AST_TYPE_KIND(c->ast, to) != AST_TYPE_POINTER && AST_TYPE_KIND(c->ast, to) != AST_TYPE_ARRAY &&
		    !(AST_QUALIFIED_TYPE_QUALIFIERS(to) & AST_QUALIFIER_CONST) && from != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, from) != AST_TYPE_VOID) {
			cxx_scope *cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to));
			cxx_entity *set, *own = NULL;
			uint8_t lv = !rvalue;
			int rank;

			if (!cls)
				return false;
			cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to));
			if (cls->has_dependent_base)
				return false;
			set = cxx_lookup_in(c, cls, vintern_cstr("operator="), 0);
			for (cxx_entity *e = set; e; e = e->next)
				if (e->member_of == cls || e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
					own = set;
					break;
				}
			if (own) {
				ast_qualified_type et = c->etype;
				bool el = c->elval;
				cxx_entity *callee = c->ecallee;
				uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;
				bool resolved, none;

				c->call_obj = 1, c->no_literal = 1;
				cxx_call_type_impl(c, own, CXX_TY_UNKNOWN, NULL, UINT32_MAX, &from, &lv, 1, true);
				resolved = c->call_resolved || c->etype != CXX_TY_UNKNOWN, none = c->none_viable;
				c->etype = et, c->elval = el, c->ecallee = callee;
				c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing, c->einvalid_unsure = adl;
				if (resolved) {
					*v = !(c->call_picked && c->call_picked->kind == CXX_ENTITY_VALUE && c->call_picked->fn_deleted);
					return true;
				}
				if (!none)
					return false;
			}
			c->rank_no_literal = 1;
			rank = cxx_arg_rank0(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), from, lv, true);
			c->rank_no_literal = 0;
			if (rank == 2)
				return false;
			if (rank == 3) {
				*v = 0;
				return true;
			}
			{
				int r = cxx_class_special(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to), 1, true, 0);	/* (from the converted temporary) */

				if (r < 0)
					return false;
				*v = r;
				return true;
			}
		}
		if (assign)
			return false;
	}
	/* from std::nullptr_t (a struct here, no class): a pointer or nullptr_t takes it, bool only by direct
	   initialization, nothing else */
	if (nt == 2 && c->nullptr_ty && (IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_trivially_constructible") ||
					 IS("__is_convertible") || IS("__is_convertible_to") || IS("__is_nothrow_convertible"))) {
		bool ctor = IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_trivially_constructible");
		ast_qualified_type to = ctor ? t[0] : t[1], from = ctor ? t[1] : t[0];

		if (from != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, from) == AST_TYPE_POINTER && AST_TYPE(c->ast, from)->ext)
			from = AST_TYPE(c->ast, from)->base;
		if (from != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(from) == c->nullptr_ty && to != CXX_TY_UNKNOWN) {
			ast_qualified_type tb = AST_TYPE_KIND(c->ast, to) == AST_TYPE_POINTER && AST_TYPE(c->ast, to)->ext ? AST_TYPE(c->ast, to)->base : to;
			bool ref = tb != to;

			if (!ref || (AST_QUALIFIED_TYPE_QUALIFIERS(tb) & AST_QUALIFIER_CONST) || AST_TYPE(c->ast, to)->ext == 2) {
				int k = AST_TYPE_KIND(c->ast, tb);

				*v = (k == AST_TYPE_POINTER && !AST_TYPE(c->ast, tb)->ext) || AST_QUALIFIED_TYPE_UNQUALIFIED(tb) == c->nullptr_ty || (ctor && !ref && k == AST_TYPE_BOOL);
				if (k == AST_TYPE_POINTER || AST_QUALIFIED_TYPE_UNQUALIFIED(tb) == c->nullptr_ty || !cc_type_is_record(c->ast, tb))
					return true;
			} else if (!cc_type_is_record(c->ast, tb)) {
				*v = 0;	/* (T& not const from a prvalue) */
				return true;
			}
		}
	}
	/* a class from arguments: its constructors decide */
	if (nt >= 1 && nt <= 2 && (IS("__is_constructible") || IS("__is_nothrow_constructible") || (nt == 2 && (IS("__is_convertible") ||
	    IS("__is_convertible_to") || IS("__is_nothrow_convertible"))))) {
		bool conv = !IS("__is_constructible") && !IS("__is_nothrow_constructible");
		bool nothrow = IS("__is_nothrow_constructible") || IS("__is_nothrow_convertible");
		ast_qualified_type to = conv ? t[1] : t[0], from = nt == 2 ? (conv ? t[0] : t[1]) : CXX_TY_UNKNOWN;
		uint8_t lv = 0;

		if (nt == 2 && AST_TYPE_KIND(c->ast, from) == AST_TYPE_POINTER && AST_TYPE(c->ast, from)->ext) {
			lv = AST_TYPE(c->ast, from)->ext == 1;
			from = AST_TYPE(c->ast, from)->base;
		}
		if (AST_TYPE_KIND(c->ast, to) == AST_TYPE_POINTER && AST_TYPE(c->ast, to)->ext && nt == 2 && !conv) {
			/* T&/const T&/T&& from a class object: a reference to it or to a base binds (or not) */
			const ctype *tt = AST_TYPE(c->ast, to);

			if (cc_type_is_record(c->ast, tt->base) && cc_type_is_record(c->ast, from) &&
			    (AST_QUALIFIED_TYPE_UNQUALIFIED(tt->base) == AST_QUALIFIED_TYPE_UNQUALIFIED(from) || (cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from)) && cxx_derives(c, from, tt->base, 0)))) {
				bool cq = !(AST_QUALIFIED_TYPE_QUALIFIERS(from) & AST_QUALIFIER_CONST) || (AST_QUALIFIED_TYPE_QUALIFIERS(tt->base) & AST_QUALIFIER_CONST);

				*v = cq && (tt->ext == 1 ? lv || (AST_QUALIFIED_TYPE_QUALIFIERS(tt->base) & AST_QUALIFIER_CONST) : !lv);
				return true;
			}
			/* T& (not const) from an unrelated class with no conversion function: nothing binds */
			if (cc_type_is_record(c->ast, tt->base) && cc_type_is_record(c->ast, from) && tt->ext == 1 && !(AST_QUALIFIED_TYPE_QUALIFIERS(tt->base) & AST_QUALIFIER_CONST)) {
				cxx_scope *fs = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from));

				if (fs)
					cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from));
				if (fs && !cxx_class_converts(c, fs, 0)) {
					*v = 0;
					return true;
				}
			}
		}
		if (conv && nt == 2 && from != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, from) && cc_type_is_record(c->ast, to) &&
		    AST_TYPE_KIND(c->ast, from) != AST_TYPE_ARRAY && AST_TYPE_KIND(c->ast, to) != AST_TYPE_ARRAY && AST_QUALIFIED_TYPE_UNQUALIFIED(from) != AST_QUALIFIED_TYPE_UNQUALIFIED(to)) {
			/* one class to another: only derived to base, a constructor of To or a conversion function of From;
			   none of them (iterator tags: random_access_iterator_tag to contiguous_iterator_tag): no */
			cxx_scope *fs = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from)), *ts = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to));

			if (fs && ts && !fs->has_dependent_base && !ts->has_dependent_base) {
				bool any_ctor = false;

				cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from)), cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to));
				for (cxx_entity *e = cxx_lookup_in(c, ts, cxx_ctor_id(), 0); e; e = e->next)
					if (e->member_of == ts && !e->fn_implicit && !e->fn_defaulted)
						any_ctor = true;
				if (!any_ctor && !cxx_derives(c, from, to, 0) && !cxx_class_converts(c, fs, 0)) {
					*v = 0;
					return true;
				}
			}
		}
		if (cc_type_is_record(c->ast, to) && AST_TYPE_KIND(c->ast, to) != AST_TYPE_POINTER &&
		    (nt == 1 || (from != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, from) != AST_TYPE_VOID))) {
			int r;

			c->no_literal = 1;
			r = cxx_class_constructs(c, to, &from, &lv, nt - 1, conv);
			c->no_literal = 0;

			if (r < 0 || (r && nothrow && c->constructs_nothrow < 0))
				return false;
			*v = r && (!nothrow || c->constructs_nothrow == 1);	/* (nothrow: the constructor found is noexcept) */
			return true;
		}
		/* a class to something that is not one: its conversion functions (none: no) */
		if (nt == 2 && cc_type_is_record(c->ast, from) && !cc_type_is_record(c->ast, to) && AST_TYPE_KIND(c->ast, to) != AST_TYPE_VOID &&
		    !(AST_TYPE_KIND(c->ast, to) == AST_TYPE_POINTER && AST_TYPE(c->ast, to)->ext)) {
			cxx_scope *fs = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from));

			if (fs)
				cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(from));
			if (!fs || fs->has_dependent_base)
				return false;
			{
				int k = cxx_class_converts_to(c, fs, AST_QUALIFIED_TYPE_UNQUALIFIED(to), 0);

				if (k < 0)
					return false;
				*v = k;
			}
			return true;
		}
	}
	/* how two types relate, where the answer does not need a class's constructors or conversion functions */
	if (nt == 2 && IS("__is_base_of")) {
		if (!cc_type_is_record(c->ast, t[0]) || !cc_type_is_record(c->ast, t[1])) {
			*v = 0;
			return true;
		}
		if (AST_QUALIFIED_TYPE_UNQUALIFIED(t[0]) == AST_QUALIFIED_TYPE_UNQUALIFIED(t[1])) {
			*v = 1;
			return true;
		}
		if (!cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t[1])))
			return false;	/* (the derived class's bases: not known) */
		*v = cxx_derives(c, t[1], t[0], 0);
		return true;
	}
	/* T from one T (a copy or a move): not trivial with a user-provided copy or move constructor */
	if (nt == 2 && IS("__is_trivially_constructible") && t[0] != CXX_TY_UNKNOWN && t[1] != CXX_TY_UNKNOWN &&
	    cc_type_is_record(c->ast, t[0]) && AST_TYPE_KIND(c->ast, t[0]) != AST_TYPE_ARRAY) {
		ast_qualified_type from = t[1];

		if (AST_TYPE_KIND(c->ast, from) == AST_TYPE_POINTER && AST_TYPE(c->ast, from)->ext)
			from = AST_TYPE(c->ast, from)->base;
		/* (or a defaulted one that is not trivial: a member's or a base's is not, vcc's trivial-ABI test; it
		   counts a destructor that is not trivial too) */
		if (AST_QUALIFIED_TYPE_UNQUALIFIED(from) == AST_QUALIFIED_TYPE_UNQUALIFIED(t[0]) && (cxx_class_user_special(c, t[0], cxx_ctor_id()) || cxx_gen_trivial_abi(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t[0]), 0) == 0)) {
			*v = 0;
			return true;
		}
	}
	if (nt == 2 && (IS("__is_convertible") || IS("__is_convertible_to") || IS("__is_nothrow_convertible") || IS("__is_constructible") ||
			IS("__is_nothrow_constructible") || IS("__is_trivially_constructible"))) {
		/* constructible is (To, From); convertible is (From, To) */
		bool ctor = IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_trivially_constructible");
		ast_qualified_type to = ctor ? t[0] : t[1], from = ctor ? t[1] : t[0];
		const ctype *ft = AST_TYPE(c->ast, from), *tt = AST_TYPE(c->ast, to);
		bool lv = ft->kind == AST_TYPE_POINTER && ft->ext == 1;
		int rank;

		if (ft->kind == AST_TYPE_POINTER && ft->ext)
			from = ft->base;
		if (tt->kind == AST_TYPE_VOID || AST_TYPE_KIND(c->ast, from) == AST_TYPE_VOID) {
			*v = !ctor && tt->kind == AST_TYPE_VOID && AST_TYPE_KIND(c->ast, from) == AST_TYPE_VOID;
			return true;
		}
		if (tt->kind == AST_TYPE_ARRAY || tt->kind == AST_TYPE_FUNCTION) {
			*v = 0;
			return true;
		}
		/* (an integer type is not a null pointer constant: only the literal 0 is) */
		if (tt->kind == AST_TYPE_POINTER && !tt->ext && AST_TYPE_KIND(c->ast, from) >= AST_TYPE_BOOL && AST_TYPE_KIND(c->ast, from) <= AST_TYPE_FLOAT128) {
			*v = 0;
			return true;
		}
		c->rank_no_literal = 1;
		rank = cxx_arg_rank0(c, to, from, lv, true);
		c->rank_no_literal = 0;
		if (rank == 2)
			return false;	/* (through a class: its constructors and conversion functions decide) */
		*v = rank < 3 || rank == 5;
		return true;
	}
	if (nt != 1)
		return false;
	x = AST_TYPE(c->ast, t[0]);
	k = x->kind;
	ref = k == AST_TYPE_POINTER && x->ext;
	/* T(): scalars and pointers yes, references, void, functions and arrays of unknown bound no; a class's
	   default constructor decides for a class */
	if (IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_default_constructible")) {
		if (k == AST_TYPE_ARRAY) {
			/* T[N](): as its element (char[8] in json's is_getable<json, char[8]>); T[]: no */
			ast_qualified_type e = t[0];

			while (AST_TYPE_KIND(c->ast, e) == AST_TYPE_ARRAY && AST_TYPE(c->ast, e)->n)
				e = AST_TYPE(c->ast, e)->base;
			if (AST_TYPE_KIND(c->ast, e) == AST_TYPE_ARRAY) {
				*v = 0;
				return true;
			}
			return cxx_trait_value(c, n, len, &e, 1, v);
		}
		if ((k >= AST_TYPE_BOOL && k <= AST_TYPE_FLOAT128) || (k == AST_TYPE_POINTER && !x->ext) || k == AST_TYPE_ENUM) {
			*v = 1;
			return true;
		}
		if (ref || k == AST_TYPE_VOID || k == AST_TYPE_FUNCTION) {
			*v = 0;
			return true;
		}
		return false;
	}
	/* is a == b the comparison of their bytes (clang's builtin; for clang: integers, characters, bool,
	   enumerations, pointers, a class with a defaulted operator== and no padding).  vcc says no for every type:
	   what libc++ does with yes is __find_vectorized and the like, on ext_vector_type vectors, which vcc's
	   C++ front end does not have yet; no takes its element-by-element loops, which are always right */
	if (IS("__is_trivially_equality_comparable")) {
		*v = 0;
		return true;
	}
	/* destructible: a reference, a scalar, an array of known bound of such or of a class, a class whose
	   destructor is not deleted (its destructor's noexcept: implicitly so; libc++ declares none noexcept(false));
	   not void, a function, an array of unknown bound */
	if (IS("__is_destructible") || IS("__is_nothrow_destructible")) {
		ast_qualified_type e = t[0];

		if (e == CXX_TY_UNKNOWN)
			return false;
		if (AST_TYPE_KIND(c->ast, e) == AST_TYPE_POINTER && AST_TYPE(c->ast, e)->ext) {
			*v = 1;
			return true;
		}
		if (AST_TYPE_KIND(c->ast, e) == AST_TYPE_ARRAY && !AST_TYPE(c->ast, e)->n) {
			*v = 0;
			return true;
		}
		while (AST_TYPE_KIND(c->ast, e) == AST_TYPE_ARRAY)
			e = AST_TYPE(c->ast, e)->base;
		x = AST_TYPE(c->ast, e);
		if (x->kind == AST_TYPE_VOID || x->kind == AST_TYPE_FUNCTION) {
			*v = 0;
			return true;
		}
		if (cc_type_is_record(c->ast, e)) {
			cxx_scope *ds = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(e));
			cxx_entity *d;

			if (!(c->ast->tags[x->aux].flags & AST_RECORD_FLAG_COMPLETE) && !c->tentative) {
				cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(e));	/* (an instance not built yet: __wrap_iter<int *>) */
				x = AST_TYPE(c->ast, e), ds = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(e));
			}
			d = ds ? cxx_lookup_local(ds, cxx_dtor_id()) : NULL;
			if (!(c->ast->tags[x->aux].flags & AST_RECORD_FLAG_COMPLETE))
				return false;
			*v = !(d && d->fn_deleted);
			return true;
		}
		*v = 1;
		return true;
	}
	/* what a type can do trivially: yes for scalars and arrays of them, and for a class vcc laid out as POD
	   (one that is not POD for layout may still be trivially copyable: not known) */
	{
		bool trivially = IS("__is_trivially_copyable") || IS("__is_trivial") || IS("__is_standard_layout") || IS("__is_pod") ||
				 IS("__is_trivially_destructible") || IS("__is_trivially_constructible") || IS("__has_trivial_destructor") ||
				 IS("__is_literal_type") || IS("__is_trivially_relocatable");
		bool never = IS("__is_empty") || IS("__is_polymorphic") || IS("__is_abstract");

		if (trivially || never) {
			ast_qualified_type e = t[0];
			bool pod, empty;

			if (e != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, e) == AST_TYPE_POINTER && AST_TYPE(c->ast, e)->ext) {
				/* a reference: destroyed trivially (nothing to destroy), a literal type; not an object type,
				   so none of the others (clang's answers; is_empty<int&> in tuple<int&>'s leaves) */
				*v = IS("__is_trivially_destructible") || IS("__has_trivial_destructor") || IS("__is_literal_type");
				return true;
			}
			while (AST_TYPE_KIND(c->ast, e) == AST_TYPE_ARRAY)
				e = AST_TYPE(c->ast, e)->base;
			x = AST_TYPE(c->ast, e);
			if ((x->kind >= AST_TYPE_BOOL && x->kind <= AST_TYPE_FLOAT128) || (x->kind == AST_TYPE_POINTER && !x->ext) || x->kind == AST_TYPE_ENUM) {
				*v = trivially;
				return true;
			}
			if (cxx_class_layout_info(c, e, &pod, &empty)) {
				if (IS("__is_empty") && e == t[0]) {
					*v = empty;
					return true;
				}
				if (trivially && pod) {
					*v = 1;
					return true;
				}
			}
			if ((IS("__is_trivially_copyable") || IS("__is_trivially_relocatable") || IS("__is_trivial")) && cc_type_is_record(c->ast, e) &&
			    cxx_gen_trivial_abi(c, e, 0) == 0) {
				/* a copy or move constructor or destructor that is not trivial: not trivially copyable (nor
				   relocatable: [[clang::trivial_abi]] is not read) */
				*v = 0;
				return true;
			}
			if (IS("__is_trivially_relocatable") && cc_type_is_record(c->ast, e) && !cxx_class_polymorphic(c, e) &&
			    cxx_gen_trivial_abi(c, e, 0) == 1) {
				/* trivial copy and move constructors and destructor: its bytes may be moved (libc++'s relocation
				   by memcpy is right exactly then) */
				*v = 1;
				return true;
			}
			if ((IS("__is_trivially_copyable") || IS("__is_trivial")) && cc_type_is_record(c->ast, e) &&
			    cxx_class_assign_trivial(c, e, 0) == 0) {
				*v = 0;	/* (a user-declared copy or move assignment, its own or a base's or member's: not trivial) */
				return true;
			}
			if (IS("__is_trivially_copyable") && cc_type_is_record(c->ast, e) && !cxx_class_polymorphic(c, e) &&
			    !cxx_class_vbases(c, e, NULL, NULL, 0) && cxx_gen_trivial_abi(c, e, 0) == 1 && cxx_class_assign_trivial(c, e, 0) == 1) {
				/* trivial copy and move constructors, assignments and destructor ([class.prop]/1) */
				*v = 1;
				return true;
			}
			return false;
		}
	}
	if (IS("__datasizeof")) {
		uint64_t ds, size;
		uint32_t align;

		if (cxx_class_data_size(c, t[0], &ds)) {
			*v = (int64_t)ds;
			return true;
		}
		if (cc_type_is_record(c->ast, t[0]) || !cxx_type_size_known(c, t[0], &size, &align))
			return false;
		*v = (int64_t)size;
		return true;
	}
	if (IS("__is_final")) {
		cxx_scope *fs = cc_type_is_record(c->ast, t[0]) && k != AST_TYPE_ARRAY ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t[0])) : NULL;

		if (cc_type_is_record(c->ast, t[0]) && k != AST_TYPE_ARRAY && !fs)
			return false;
		*v = fs && fs->is_final;
		return true;
	}
	if (IS("__is_void")) *v = k == AST_TYPE_VOID;
	else if (IS("__is_integral")) *v = (k >= AST_TYPE_BOOL && k <= AST_TYPE_UINT128);
	else if (IS("__is_floating_point")) *v = (k >= AST_TYPE_BFLOAT16 && k <= AST_TYPE_FLOAT128);
	else if (IS("__is_arithmetic")) *v = (k >= AST_TYPE_BOOL && k <= AST_TYPE_FLOAT128);
	else if (IS("__is_pointer")) *v = k == AST_TYPE_POINTER && !x->ext;
	else if (IS("__is_reference")) *v = ref;
	else if (IS("__is_lvalue_reference")) *v = ref && x->ext == 1;
	else if (IS("__is_rvalue_reference")) *v = ref && x->ext == 2;
	else if (IS("__is_const")) *v = !ref && (AST_QUALIFIED_TYPE_QUALIFIERS(t[0]) & AST_QUALIFIER_CONST) != 0;
	else if (IS("__is_volatile")) *v = !ref && (AST_QUALIFIED_TYPE_QUALIFIERS(t[0]) & AST_QUALIFIER_VOLATILE) != 0;
	else if (IS("__is_array")) *v = k == AST_TYPE_ARRAY;
	else if (IS("__is_bounded_array")) *v = k == AST_TYPE_ARRAY && AST_TYPE(c->ast, t[0])->n && !(AST_TYPE(c->ast, t[0])->flags & AST_TYPE_FLAG_INCOMPLETE);
	else if (IS("__is_unbounded_array")) *v = k == AST_TYPE_ARRAY && (!AST_TYPE(c->ast, t[0])->n || (AST_TYPE(c->ast, t[0])->flags & AST_TYPE_FLAG_INCOMPLETE));
	else if (IS("__is_function")) *v = k == AST_TYPE_FUNCTION;
	else if (IS("__is_enum")) *v = k == AST_TYPE_ENUM;
	else if (IS("__is_union")) *v = k == AST_TYPE_UNION;
	else if (IS("__is_class")) *v = k == AST_TYPE_STRUCT;
	else if (IS("__is_signed")) *v = (k >= AST_TYPE_BFLOAT16 && k <= AST_TYPE_FLOAT128) || (k >= AST_TYPE_CHAR && k <= AST_TYPE_UINT128 && cc_type_is_signed(c->ast, t[0]));
	else if (IS("__is_unsigned")) *v = k == AST_TYPE_BOOL || (k >= AST_TYPE_CHAR && k <= AST_TYPE_UINT128 && !cc_type_is_signed(c->ast, t[0]));
	else if (IS("__is_fundamental")) *v = k <= AST_TYPE_FLOAT128;
	else if (IS("__is_scalar")) *v = (k >= AST_TYPE_BOOL && k <= AST_TYPE_FLOAT128) || (k == AST_TYPE_POINTER && !x->ext) || k == AST_TYPE_ENUM;
	else if (IS("__is_object")) *v = k != AST_TYPE_FUNCTION && k != AST_TYPE_VOID && !ref;
	else if (IS("__is_compound")) *v = k > AST_TYPE_FLOAT128;
	else
		return false;
#undef IS
	return true;
}

/* a range-for's element: *begin(r) for an array or a class with begin() (an lvalue's type; ADL begin: not yet) */
VPRIV bool cxx_range_element_type(cxx *c, ast_qualified_type r, ast_qualified_type *elem)
{
	const ctype *x;
	cxx_scope *cls;
	cxx_entity *set;
	ast_qualified_type it;

	if (r == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, r);
	if (x->kind == AST_TYPE_ARRAY) {
		*elem = x->base;
		return true;
	}
	if (!(cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(r))))
		return false;
	set = cxx_lookup_in(c, cls, vintern_cstr("begin"), 0);
	if (!set || set->kind == CXX_ENTITY_DEPENDENT)
		return false;
	c->call_obj = (AST_QUALIFIED_TYPE_QUALIFIERS(r) & AST_QUALIFIER_CONST) ? 2 : 1;
	cxx_call_type(c, set, CXX_TY_UNKNOWN, NULL, NULL, 0);
	it = c->etype;
	if (it == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, it);
	if (x->kind == AST_TYPE_POINTER && !x->ext) {
		*elem = x->base;
		return true;
	}
	/* an iterator class: its operator*() */
	if (!cxx_operator_call_type(c, vintern_cstr("operator*"), it, NULL, 0) || c->etype == CXX_TY_UNKNOWN)
		return false;
	*elem = c->etype;
	return true;
}

/* argument-dependent lookup: name in the namespace of each argument's class or enumeration (the first that has it) */
/* does a set found by name have a function or function template in it (all that argument-dependent lookup
   finds, [basic.lookup.argdep]/4: not a variable, a function object) */
static bool cxx_adl_has_functions(cxx *c, const cxx_entity *set)
{
	for (const cxx_entity *e = set; e; e = e->next)
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE ||
		    (e->kind == CXX_ENTITY_VALUE && (e->ty == CXX_TY_UNKNOWN ? e->has_ret_pattern : AST_TYPE_KIND(c->ast, e->ty) == AST_TYPE_FUNCTION)))
			return true;
	return false;
}

VPRIV void cxx_adl_call_type(cxx *c, uint32_t name, const ast_qualified_type *args, const uint8_t *lval, uint32_t nargs)
{
	ast_qualified_type acopy[16];
	uint8_t lcopy[16];
	/* a resolution that needed a conversion (std's operator<< for a json, through its conversion function)
	   is set aside while the other arguments' namespaces and friends are tried: one that converts no argument
	   worse and some better wins (one candidate set in C++) */
	bool held = false, h_lval = false;
	ast_qualified_type h_type = CXX_TY_UNKNOWN;
	cxx_entity *h_picked = NULL, *h_callee = NULL;
	uint8_t h_ranks[16], h_n = 0, h_unsure = 0;
#define ADL_DONE() do { \
		if (c->etype != CXX_TY_UNKNOWN) { \
			bool conv = false; \
			for (uint32_t r_ = 0; r_ < c->call_nranks; r_++) \
				conv |= c->call_ranks[r_] >= 2; \
			if (held) { \
				bool better = !h_n && c->call_resolved, worse = !c->call_resolved || (h_n && c->call_nranks != h_n); \
				for (uint32_t r_ = 0; h_n && r_ < h_n && !worse; r_++) \
					better |= c->call_ranks[r_] < h_ranks[r_], worse |= c->call_ranks[r_] > h_ranks[r_]; \
				if (better && !worse) \
					return; \
				goto restore; \
			} \
			/* (not resolved, one type shared by candidates vcc could not order: held too, h_n 0) */ \
			if (i + 1 >= nargs || (c->call_resolved && (!conv || !c->call_nranks || c->call_nranks > 16))) \
				return; \
			if (!c->call_resolved) \
				c->call_nranks = 0; \
			held = true, h_type = c->etype, h_lval = c->elval, h_picked = c->call_picked, h_callee = c->ecallee; \
			h_n = c->call_nranks, h_unsure = c->eunsure; \
			memcpy(h_ranks, c->call_ranks, h_n); \
		} \
	} while (0)

	c->einvalid_unsure = 0;
	if (nargs > 16 || (c->tentative && !c->unevaluated)) {
		c->einvalid_unsure = 1;
		return;
	}
	/* (in an unevaluated operand of a tentative parse too: decltype(operator<<(os, x)) in a partial
	   specialization, doctest's has_insertion_operator, finds a hidden friend only this way) */
	memcpy(acopy, args, nargs * sizeof *args), memcpy(lcopy, lval, nargs);
	uint8_t unsure0 = c->eunsure;	/* (each attempt's own verdict: an earlier one's unsure set does not stay on a later pick) */
	for (uint32_t i = 0; i < nargs; i++) {
		ast_qualified_type t = acopy[i];
		cxx_scope *s;
		cxx_entity *set;

		if (t == CXX_TY_UNKNOWN)
			continue;
		if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER)
			t = AST_TYPE(c->ast, t)->base;
		if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == AST_TYPE_ENUM) {
			/* an enumeration: the innermost namespace of its declaration ([basic.lookup.argdep]/3.3; a member
			   enumeration's class too, not looked in here) */
			s = cxx_tag_home(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
			while (s && s->kind != CXX_SCOPE_NAMESPACE)
				s = s->parent;
			if (!s || !(set = cxx_lookup_in(c, s, name, 0)) || set->kind == CXX_ENTITY_DEPENDENT || !cxx_adl_has_functions(c, set))
				continue;
			c->eunsure = unsure0, cxx_call_type(c, set, CXX_TY_UNKNOWN, acopy, lcopy, nargs);
			ADL_DONE();
			if (c->etype != CXX_TY_UNKNOWN)
				continue;
			if (!c->none_viable)
				c->einvalid_unsure = 1;
			continue;
		}
		if (t != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY)
			cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));	/* (an instance not built yet: its friends) */
		if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || !(s = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t))))
			continue;
		/* (a friend the class declares: swap(a, b), to_json(j, x) defined in the class) */
		if ((set = cxx_friend_lookup(c, t, name, 0))) {
			c->eunsure = unsure0, cxx_call_type(c, set, CXX_TY_UNKNOWN, acopy, lcopy, nargs);
			ADL_DONE();
			if (c->etype == CXX_TY_UNKNOWN && !c->none_viable)
				c->einvalid_unsure = 1;
		}
		while (s && s->kind != CXX_SCOPE_NAMESPACE)
			s = s->parent;
		{
			/* the class's namespace, then its base classes' ([basic.lookup.argdep]/3) */
			cxx_scope *nsl[9];
			ast_qualified_type b[8];
			uint32_t nns = 0, nb = cxx_class_bases(c, t, b, 8);

			if (s)
				nsl[nns++] = s;
			for (uint32_t k = 0; k < nb; k++) {
				cxx_scope *bs = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(b[k]));
				bool dup = false;

				while (bs && bs->kind != CXX_SCOPE_NAMESPACE)
					bs = bs->parent;
				for (uint32_t m = 0; m < nns; m++)
					dup |= nsl[m] == bs;
				if (bs && !dup)
					nsl[nns++] = bs;
			}
			for (uint32_t k = 0; k < nns; k++) {
				if (!(set = cxx_lookup_in(c, nsl[k], name, 0)) || set->kind == CXX_ENTITY_DEPENDENT || !cxx_adl_has_functions(c, set))
					continue;	/* (what ADL finds is functions only: json's nlohmann::from_json, an object, is not one) */
				c->eunsure = unsure0, cxx_call_type(c, set, CXX_TY_UNKNOWN, acopy, lcopy, nargs);
				ADL_DONE();	/* (the caller decides what this means for the expression's validity) */
				if (c->etype == CXX_TY_UNKNOWN && !c->none_viable)
					c->einvalid_unsure = 1;	/* (candidates vcc could not decide between) */
			}
		}
	}
	if (held) {
	restore:
		cxx_et(c, h_type, h_lval);
		c->call_resolved = h_picked != NULL, c->call_picked = h_picked, c->ecallee = h_callee, c->eunsure = h_unsure, c->none_viable = 0;
		c->call_nranks = h_n;
		memcpy(c->call_ranks, h_ranks, h_n);
		return;
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
#undef ADL_DONE
}


/*
 * Does one of the class's conversion functions (or a base's) convert to `to`, a type that is not a
 * class: 1 yes (its type converts to `to` by a standard conversion), 0 no, -1 not known.
 */
/* are the class's copy and move assignments trivial: 1 none user-declared in it, its bases or its members of
   class type (arrays of them too), 0 one is (not trivial), -1 not known (a reference or const member: deleted) */
static int cxx_class_assign_trivial(cxx *c, ast_qualified_type t, int depth)
{
	cxx_scope *cls = cxx_tag_scope_get(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t));
	ast_qualified_type bases[16];
	uint32_t nb;
	int r = 1;

	if (!cls || depth > 8)
		return -1;
	if (cxx_class_user_special(c, t, vintern_cstr("operator=")))
		return 0;
	nb = cxx_class_bases(c, t, bases, 16);
	for (uint32_t i = 0; i < nb; i++) {
		int k = cxx_class_assign_trivial(c, bases[i], depth + 1);

		if (k == 0)
			return 0;
		if (k < 0)
			r = -1;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type ft = cls->field_types[i];

		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(c->ast, ft)->base;
		if (ft == CXX_TY_UNKNOWN || (AST_TYPE_KIND(c->ast, ft) == AST_TYPE_POINTER && AST_TYPE(c->ast, ft)->ext) ||
		    (AST_QUALIFIED_TYPE_QUALIFIERS(ft) & AST_QUALIFIER_CONST)) {
			r = -1;
			continue;
		}
		if (cc_type_is_record(c->ast, ft)) {
			int k = cxx_class_assign_trivial(c, ft, depth + 1);

			if (k == 0)
				return 0;
			if (k < 0)
				r = -1;
		}
	}
	return r;
}

/* are the class's copy and move assignments trivial (1), not (0), not known (-1) */
VPRIV int cxx_class_assignment_trivial(cxx *c, ast_qualified_type t)
{
	return cxx_class_assign_trivial(c, t, 0);
}

/* the one scalar type (arithmetic, an enumeration, a pointer) class t's conversion functions (or a base's)
   give, for a built-in operator its object is an operand of (atomic<int> == 4); CXX_TY_UNKNOWN: none, several
   or not known */
static ast_qualified_type cxx_class_scalar_conversion_in(cxx *c, cxx_scope *cls, int depth, bool *several)
{
	ast_qualified_type found = CXX_TY_UNKNOWN;

	if (!cls || cls->has_dependent_base || depth > 8 || cls->conv_unknown) {
		*several = true;
		return CXX_TY_UNKNOWN;
	}
	for (uint32_t i = 0; cls->has_conversion && i < cls->nconv; i++) {
		ast_qualified_type t = cls->conv_types[i];

		if (t == CXX_TY_UNKNOWN) {
			*several = true;
			continue;
		}
		if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
			t = AST_TYPE(c->ast, t)->base;
		if (cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY)
			continue;
		if (found != CXX_TY_UNKNOWN && AST_QUALIFIED_TYPE_UNQUALIFIED(found) != AST_QUALIFIED_TYPE_UNQUALIFIED(t))
			*several = true;
		found = AST_QUALIFIED_TYPE_UNQUALIFIED(t);
	}
	for (uint32_t i = 0; found == CXX_TY_UNKNOWN && i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS)
			found = cxx_class_scalar_conversion_in(c, cls->used[i], depth + 1, several);
	return found;
}

VPRIV ast_qualified_type cxx_class_scalar_conversion(cxx *c, ast_qualified_type t)
{
	bool several = false;
	ast_qualified_type r;

	if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || AST_TYPE_KIND(c->ast, t) == AST_TYPE_ARRAY)
		return CXX_TY_UNKNOWN;
	r = cxx_class_scalar_conversion_in(c, cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)), 0, &several);
	return several ? CXX_TY_UNKNOWN : r;
}

static int cxx_class_converts_to(cxx *c, cxx_scope *cls, ast_qualified_type to, int depth)
{
	int r = 0;

	if (!cls || cls->has_dependent_base || depth > 8)
		return -1;
	if (cls->has_conversion) {
		if (cls->conv_unknown || !cls->nconv)
			r = -1;
		for (uint32_t i = 0; i < cls->nconv; i++) {
			ast_qualified_type t = cls->conv_types[i];
			const ctype *x = AST_TYPE(c->ast, t);
			int k;

			if (x->kind == AST_TYPE_POINTER && x->ext)
				t = x->base;
			if (cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY)
				continue;	/* (to a class: a second user-defined conversion would be needed) */
			{
				uint8_t nl = c->rank_no_literal;

				c->rank_no_literal = 1;	/* (what a conversion function returns is no literal 0) */
				k = cxx_arg_rank0(c, to, t, false, false);
				c->rank_no_literal = nl;
			}
			if (k <= 1)
				return 1;
			if (k == 2)
				r = -1;
		}
	}
	for (uint32_t i = 0; i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS) {
			int k = cxx_class_converts_to(c, cls->used[i], to, depth + 1);

			if (k > 0)
				return 1;
			if (k < 0)
				r = -1;
		}
	return r;
}

/* may one of the class's conversion functions (or a base's) convert to the class `to` (or one derived from it)? */
static bool cxx_class_converts_class(cxx *c, cxx_scope *cls, ast_qualified_type to, int depth)
{
	if (!cls || cls->has_dependent_base || depth > 8)
		return true;
	if (cls->has_conversion) {
		if (cls->conv_unknown || !cls->nconv)
			return true;
		for (uint32_t i = 0; i < cls->nconv; i++) {
			ast_qualified_type t = cls->conv_types[i];

			if (AST_TYPE_KIND(c->ast, t) == AST_TYPE_POINTER && AST_TYPE(c->ast, t)->ext)
				t = AST_TYPE(c->ast, t)->base;
			if (cc_type_is_record(c->ast, t) && AST_TYPE_KIND(c->ast, t) != AST_TYPE_ARRAY &&
			    (AST_QUALIFIED_TYPE_UNQUALIFIED(t) == AST_QUALIFIED_TYPE_UNQUALIFIED(to) || cxx_derives(c, t, to, 0)))
				return true;
		}
	}
	for (uint32_t i = 0; i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS && cxx_class_converts_class(c, cls->used[i], to, depth + 1))
			return true;
	return false;
}

/* does the class, or a base of it, declare a conversion function? */
static bool cxx_class_converts(cxx *c, cxx_scope *cls, int depth)
{
	if (cls->has_conversion || cls->has_dependent_base || depth > 12)
		return true;
	for (uint32_t i = 0; i < cls->nused; i++)
		if (cls->used[i] && cls->used[i]->kind == CXX_SCOPE_CLASS && cxx_class_converts(c, cls->used[i], depth + 1))
			return true;
	return false;
}

/*
 * Can an object of the class `to` be made from n arguments, T(args)?  1 yes, 0 no, -1 not known.
 * Its constructors by overload resolution; with none declared the implicit ones.
 */
static int cxx_class_constructs(cxx *c, ast_qualified_type to, const ast_qualified_type *args, const uint8_t *lval, uint32_t n, bool implicit);

/* does the implicit (or = default) constructor throw nothing: copying (copy: from a const lvalue of the class,
   moving: from an rvalue; 0 default) each base and member does not.  1 yes, 0 no, -1 not known */
static int cxx_class_parts_nothrow(cxx *c, cxx_scope *cls, int copy, int depth)
{
	int r = 1;

	if (!cls || depth > 8)
		return -1;
	for (uint32_t i = 0, nb = cls->nused, nf = cls->nfield_types; i < nb + nf && r != 0; i++) {
		ast_qualified_type ft;

		if (i < nb) {
			cxx_scope *b = cls->used[i];

			if (!b || b->kind != CXX_SCOPE_CLASS)
				continue;
			ft = b->owner ? b->owner->ty : CXX_TY_UNKNOWN;
		} else
			ft = cls->field_types[i - nb];
		while (ft != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, ft) == AST_TYPE_ARRAY)
			ft = AST_TYPE(c->ast, ft)->base;
		if (ft == CXX_TY_UNKNOWN)
			return -1;
		if (!cc_type_is_record(c->ast, ft))
			continue;	/* (a scalar, a pointer, a reference: copied without throwing) */
		{
			ast_qualified_type a = copy == 1 ? AST_QUALIFIED_TYPE(AST_QUALIFIED_TYPE_INDEX(ft), AST_QUALIFIER_CONST) : AST_QUALIFIED_TYPE_UNQUALIFIED(ft);
			uint8_t lv = copy == 1;
			int k = cxx_class_constructs(c, ft, copy ? &a : NULL, copy ? &lv : NULL, copy ? 1 : 0, false);

			if (k <= 0 || c->constructs_nothrow < 0)
				r = -1;
			else if (!c->constructs_nothrow)
				r = 0;
		}
	}
	return r;
}


static int cxx_class_constructs(cxx *c, ast_qualified_type to, const ast_qualified_type *args, const uint8_t *lval, uint32_t n, bool implicit)
{
	cxx_scope *cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to)), *from_cls = NULL;

	c->constructs_nothrow = -1;
	cxx_entity *set, *own = NULL;
	bool same = false, derived = false;
	int r = -1;

	if (!cls)
		return -1;
	cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(to));
	if (cls->has_dependent_base)
		return -1;
	if (n == 1) {
		if (args[0] == CXX_TY_UNKNOWN)
			return -1;
		if (cc_type_is_record(c->ast, args[0]) && AST_TYPE_KIND(c->ast, args[0]) != AST_TYPE_POINTER &&
		    !(c->nullptr_ty && AST_QUALIFIED_TYPE_UNQUALIFIED(args[0]) == c->nullptr_ty)) {	/* (nullptr_t: a struct here, no class: as a scalar) */
			from_cls = cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(args[0]));
			same = AST_QUALIFIED_TYPE_UNQUALIFIED(args[0]) == AST_QUALIFIED_TYPE_UNQUALIFIED(to);
			if (!same && from_cls) {
				cxx_type_require_complete(c, AST_QUALIFIED_TYPE_UNQUALIFIED(args[0]));
				derived = cxx_derives(c, args[0], to, 0);
			}
			if (!same && !from_cls)
				return -1;
		}
	}
	set = cxx_lookup_in(c, cls, cxx_ctor_id(), 0);
	for (cxx_entity *e = set; e; e = e->next)
		if (e->member_of == cls) {
			own = set;
			break;
		}
	if (own) {
		ast_qualified_type et = c->etype;
		bool el = c->elval;
		cxx_entity *callee = c->ecallee, *picked;
		uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;
		bool resolved, none;

		ast_qualified_type outer = c->constructing;

		uint8_t nl = c->no_literal;

		c->skip_explicit = implicit;
		c->call_obj = 0;
		c->no_literal = nl;
		c->constructing = n == 1 && !same && !derived ? AST_QUALIFIED_TYPE_UNQUALIFIED(to) : 0;
		cxx_call_type_impl(c, own, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, lval, n, true);
		c->constructing = outer;
		resolved = c->call_resolved, none = c->none_viable, picked = c->call_picked;
		c->etype = et, c->elval = el, c->ecallee = callee;
		c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing, c->einvalid_unsure = adl;
		if (resolved) {
			/* (whether it throws: as declared; = default and implicit ones: not throwing when the members
			   are scalars) */
			c->constructs_nothrow = !picked || picked->kind != CXX_ENTITY_VALUE ? -1 : picked->fn_noexcept == 1 ? 1 :
						picked->fn_noexcept == 2 ? -1 : picked->fn_defaulted || picked->fn_implicit ? cxx_class_parts_nothrow(c, cls, n == 0 ? 0 : 1, 0) : 0;
			return !(picked && picked->kind == CXX_ENTITY_VALUE && picked->fn_deleted);
		}
		if (!none && !same && !derived)
			return -1;
		r = 0;	/* (none of the declared ones; the implicit copy and move remain) */
	}
	if (same || derived) {
		int s = cxx_class_special(c, to, 0, !lval[0] && !(AST_QUALIFIED_TYPE_QUALIFIERS(args[0]) & AST_QUALIFIER_CONST), 0);

		c->constructs_nothrow = same && s == 1 ? cxx_class_parts_nothrow(c, cls, lval[0] || (AST_QUALIFIED_TYPE_QUALIFIERS(args[0]) & AST_QUALIFIER_CONST) ? 1 : 2, 0) : -1;	/* (the implicit copy or move) */
		return s;
	}
	if (n == 1 && from_cls && cxx_class_converts_class(c, from_cls, AST_QUALIFIED_TYPE_UNQUALIFIED(to), 0))
		return -1;	/* (through the argument's conversion function, perhaps) */
	if (own)
		return r;
	/* no constructor declared: T() if its members allow it (taken to); T(a, ...) only for an aggregate
	   (C++20: parenthesized aggregate initialization), which is not decided here */
	if (!n) {
		for (uint32_t i = 0; i < cls->nfield_types; i++)
			if (cls->field_types[i] == CXX_TY_UNKNOWN)
				return -1;
			else if (AST_TYPE_KIND(c->ast, cls->field_types[i]) == AST_TYPE_POINTER && AST_TYPE(c->ast, cls->field_types[i])->ext)
				return 0;
			else if (cc_type_is_record(c->ast, cls->field_types[i]) && AST_TYPE_KIND(c->ast, cls->field_types[i]) != AST_TYPE_ARRAY) {
				int f = cxx_class_constructs(c, cls->field_types[i], NULL, NULL, 0, false);

				if (f <= 0)
					return f;
			}
		for (uint32_t i = 0; i < cls->nused; i++) {
			cxx_scope *b = cls->used[i];

			if (b && b->kind == CXX_SCOPE_CLASS && b->owner && b->owner->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, b->owner->ty)) {
				int f = cxx_class_constructs(c, b->owner->ty, NULL, NULL, 0, false);

				if (f <= 0)
					return f;
			}
		}
		return 1;
	}
	return cls->nfield_types || cls->nused ? -1 : 0;
}

/*
 * Can an object of the class t be made from (what 0), or assigned (what 1), an object of its own type,
 * an rvalue or an lvalue?  The constructor or operator= that takes it: 1 if it is user-provided, 0 if
 * deleted (or the copy is implicitly deleted by a declared move); a defaulted or implicit one does what
 * its bases and members do.  -1: not known.
 */
VPRIV int cxx_class_special(cxx *c, ast_qualified_type t, int what, bool rvalue, int depth)
{
	cxx_scope *cls = depth < 12 ? cxx_type_class_scope(c, AST_QUALIFIED_TYPE_UNQUALIFIED(t)) : NULL;
	cxx_entity *move = NULL, *copy = NULL, *pick;
	bool templates = false;
	int result = 1;

	if (getenv("VCC_CXX_NOSPECIAL"))
		return -1;
	if (!cls || cls->has_dependent_base)
		return -1;
	for (cxx_entity *e = cxx_lookup_in(c, cls, what ? vintern_cstr("operator=") : cxx_ctor_id(), 0); e; e = e->next) {
		const ctype *ft;
		ast_qualified_type p;

		if (e->member_of != cls)
			continue;	/* (a base's: not this class's own) */
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
			templates = true;	/* (never a copy or move constructor; it may still be chosen) */
			continue;
		}
		if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != AST_TYPE_FUNCTION)
			continue;
		ft = AST_TYPE(c->ast, e->ty);
		if (!ft->n || ft->n > 1u + e->ndefaults)
			continue;
		p = c->ast->params[ft->aux];
		if (AST_TYPE_KIND(c->ast, p) == AST_TYPE_POINTER && AST_TYPE(c->ast, p)->ext) {
			const ctype *pt = AST_TYPE(c->ast, p);

			if (AST_QUALIFIED_TYPE_UNQUALIFIED(pt->base) != AST_QUALIFIED_TYPE_UNQUALIFIED(t))
				continue;
			if (pt->ext == 2)
				move = e;
			else if (AST_QUALIFIED_TYPE_QUALIFIERS(pt->base) & AST_QUALIFIER_CONST)
				copy = e;
			else if (!copy)
				copy = e;	/* X(X&): a copy constructor (from an lvalue that is not const) */
		} else if (AST_QUALIFIED_TYPE_UNQUALIFIED(p) == AST_QUALIFIED_TYPE_UNQUALIFIED(t) && what)
			move = copy = e;	/* operator=(X): both */
	}
	(void)templates;
	pick = rvalue ? (move ? move : copy) : copy;
	if (!rvalue && !copy && move)
		return 0;	/* a declared move operation: the copy is implicitly deleted */
	if (pick) {
		if (pick->fn_deleted)
			return 0;
		if (!pick->fn_defaulted)
			return 1;	/* user-provided */
	}
	/* defaulted or implicit: member-wise */
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *b = cls->used[i];
		int r;

		if (!b || b->kind != CXX_SCOPE_CLASS || !b->owner || b->owner->ty == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, b->owner->ty))
			continue;
		r = cxx_class_special(c, b->owner->ty, what, rvalue, depth + 1);
		if (!r)
			return 0;
		if (r < 0)
			result = -1;
	}
	for (uint32_t i = 0; i < cls->nfield_types; i++) {
		ast_qualified_type m = cls->field_types[i];
		const ctype *mt;
		int r = 1;

		if (m == CXX_TY_UNKNOWN) {
			result = -1;
			continue;
		}
		while (AST_TYPE_KIND(c->ast, m) == AST_TYPE_ARRAY)
			m = AST_TYPE(c->ast, m)->base;
		mt = AST_TYPE(c->ast, m);
		if (mt->kind == AST_TYPE_POINTER && mt->ext)
			r = !what;	/* a reference member: copied, not assigned */
		else if (what && (AST_QUALIFIED_TYPE_QUALIFIERS(m) & AST_QUALIFIER_CONST))
			r = 0;		/* a const member: not assigned */
		else if (mt->kind == AST_TYPE_STRUCT || mt->kind == AST_TYPE_UNION)
			r = cxx_class_special(c, m, what, rvalue && !(AST_QUALIFIED_TYPE_QUALIFIERS(m) & AST_QUALIFIER_CONST), depth + 1);
		if (!r)
			return 0;
		if (r < 0)
			result = -1;
	}
	return result;
}
