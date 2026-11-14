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
	c->edecl = t, c->edecl_name = 0;
	c->ename = 0, c->ename_missing = 0;	/* (a name only until something is done with it) */
	if (t != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, t) == TY_PTR && AST_TYPE(c->ast, t)->ext) {
		/* a reference: its referent; an lvalue for T&, and for T&& only when it is a name (lvalue set by the
		   caller): a call or cast to T&& is an xvalue, which binds as an rvalue */
		c->etype = AST_TYPE(c->ast, t)->base, c->elval = AST_TYPE(c->ast, t)->ext == 1 || lvalue;
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
	/* two values of one enumeration compared: bool (a scoped one has no other built-in operators) */
	if (l != CXX_TY_UNKNOWN && r != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, l) == TY_ENUM && QUNQ(l) == QUNQ(r) &&
	    (op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_EXCLAMATION_EQUAL || op == LEX_TOKEN_LESS || op == LEX_TOKEN_GREATER ||
	     op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL)) {
		cxx_et(c, cc_type_basic(TY_BOOL), false);
		return;
	}
	/* an unscoped enumeration in arithmetic: promoted (to int here; a scoped one has no such operators) */
	if (l != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, l) == TY_ENUM)
		l = cc_type_basic(TY_INT);
	if (r != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, r) == TY_ENUM)
		r = cc_type_basic(TY_INT);
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
		cxx_et(c, lvalue && t != CXX_TY_UNKNOWN ? cxx_type_pointer(c, t, 0) : CXX_TY_UNKNOWN, false);
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
/* is the class d derived from the class b? */
static bool cxx_derives(cxx *c, qty d, qty b, int depth)
{
	cxx_scope *cls = depth < 8 ? cxx_type_class_scope(c, QUNQ(d)) : NULL;

	if (!cls)
		return false;
	for (uint32_t i = 0; i < cls->nused; i++) {
		cxx_scope *s = cls->used[i];

		if (!s || s->kind != CXX_SCOPE_CLASS || !s->owner || s->owner->ty == CXX_TY_UNKNOWN)
			continue;
		if (QUNQ(s->owner->ty) == QUNQ(b) || cxx_derives(c, s->owner->ty, b, depth + 1))
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

static int cxx_arg_rank0(cxx *c, qty p, qty a, bool lvalue, bool have_lvalue)
{
	const ctype *pt, *at;

	if (p == CXX_TY_UNKNOWN || a == CXX_TY_UNKNOWN)
		return 2;
	pt = AST_TYPE(c->ast, p);
	if (pt->kind == TY_PTR && pt->ext) {
		/* T&& does not bind an lvalue; T& (not const) does not bind an rvalue */
		if (have_lvalue && (pt->ext == 2 ? lvalue : !lvalue && !(QQ(pt->base) & Q_CONST)))
			return 3;
		p = pt->base;
		pt = AST_TYPE(c->ast, p);
	}
	at = AST_TYPE(c->ast, a);
	if (QUNQ(p) == QUNQ(a))
		return 0;
	/* nullptr converts to any pointer (and to bool), nothing else to nullptr_t */
	if (c->nullptr_ty && QUNQ(a) == c->nullptr_ty)
		return (pt->kind == TY_PTR && !pt->ext) || pt->kind == TY_BOOL ? 1 : cc_type_is_record(c->ast, p) ? 2 : 3;
	if (c->nullptr_ty && QUNQ(p) == c->nullptr_ty)
		return 3;
	/* an array bound to a reference to array: the same length and element (its cv may differ) */
	if (pt->kind == TY_ARRAY && at->kind == TY_ARRAY)
		return pt->n == at->n && QUNQ(pt->base) == QUNQ(at->base) ? 0 : 3;
	if (((pt->kind >= TY_BOOL && pt->kind <= TY_FLOAT128) || pt->kind == TY_ENUM) &&
	    ((at->kind >= TY_BOOL && at->kind <= TY_FLOAT128) || at->kind == TY_ENUM))
		return pt->kind == TY_ENUM ? 3 : 1;	/* (nothing converts to an enumeration) */
	if (pt->kind == TY_PTR && !pt->ext) {
		const ctype *pb = AST_TYPE(c->ast, pt->base);
		qty ab;

		if (at->kind == TY_ARRAY)
			ab = at->base;
		else if (at->kind == TY_FUNC)
			return QUNQ(pt->base) == QUNQ(a) ? 0 : 3;
		else if (at->kind == TY_PTR && !at->ext)
			ab = at->base;
		else if (at->kind == TY_STRUCT || at->kind == TY_UNION) {
			/* a class to a pointer: only through a conversion function */
			cxx_scope *as = cxx_type_class_scope(c, QUNQ(a));

			if (!as)
				return 2;
			cxx_type_require_complete(c, QUNQ(a));
			return cxx_class_converts(c, as, 0) ? 2 : 3;
		} else
			return at->kind >= TY_BOOL && at->kind <= TY_ULLONG ? 2 : 3;
		if (QUNQ(pt->base) == QUNQ(ab))
			return 0;
		if (pb->kind == TY_VOID)
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
		cxx_scope *as = cc_type_is_record(c->ast, a) ? cxx_type_class_scope(c, QUNQ(a)) : NULL;
		cxx_scope *ps = cc_type_is_record(c->ast, p) ? cxx_type_class_scope(c, QUNQ(p)) : NULL;
		bool by_ctor = false, by_conv = false;

		if (cc_type_is_record(c->ast, a)) {
			if (!as)
				return 2;
			cxx_type_require_complete(c, QUNQ(a));
			by_conv = cxx_class_converts(c, as, 0);
		}
		if (cc_type_is_record(c->ast, p)) {
			if (!ps)
				return 2;
			cxx_type_require_complete(c, QUNQ(p));
			if (ps->has_dependent_base)
				return 2;
			for (cxx_entity *e = cxx_lookup_in(c, ps, cxx_ctor_id(), 0); e; e = e->next)
				by_ctor |= e->member_of == ps;
		}
		return by_ctor || by_conv ? 2 : 3;
	}
	return 3;
}

/*
 * The rank used to compare candidates: 0 exact, 1 exact but bound to a reference that adds const (f(C&)
 * is better than f(const C&) for an object that is not const, f(T&&) than f(const T&) for an rvalue),
 * 2 a standard conversion, 4 perhaps, 6 not at all.
 */
static int cxx_arg_rank(cxx *c, qty p, qty a, bool lvalue, bool have_lvalue)
{
	int r = cxx_arg_rank0(c, p, a, lvalue, have_lvalue) * 2;

	if (!r && p != CXX_TY_UNKNOWN && a != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, p) == TY_PTR && AST_TYPE(c->ast, p)->ext &&
	    (QQ(AST_TYPE(c->ast, p)->base) & ~QQ(a) & Q_CONST))
		r = 1;
	return r;
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
	cxx_entity *first = NULL;
	uint32_t viable = 0;
	uint8_t obj = c->call_obj;	/* a member call's object: const or not */
	bool have_lval = lval != NULL;

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
	/* the viable candidates, each argument's conversion ranked */
	struct cand { cxx_entity *e; qty r; uint8_t rank[16]; uint8_t ranked, tmpl; } cand[24];
	const char *tn = getenv("VCC_CXX_CAND");	/* trace the candidates of calls to this name */
	bool trace = tn && !c->template_depth && strlen(tn) == vintern_ents[set->name].len && !memcmp(tn, vintern_ents[set->name].s, strlen(tn));
	uint32_t ncand = 0;
	int best = -1;

	/* a call on a pattern's placeholders: a type perhaps, but whether it is valid depends on what they stand
	   for (a partial specialization's X<T, decltype(f(std::declval<T>()))> is substituted when matching) */
	for (uint32_t i = 0; i < nargs && i < 16 && !c->tentative; i++)
		if (args[i] != CXX_TY_UNKNOWN && cxx_type_has_placeholder(c, args[i]))
			c->eunsure = 1;
	if (trace) {
		fprintf(stderr, "call %.*s/%u tentative=%d unevaluated=%d args:", (int)vintern_ents[set->name].len, vintern_ents[set->name].s, nargs,
			c->tentative, c->unevaluated);
		for (uint32_t i = 0; i < nargs && i < 16; i++) {
			char buf[160];
			size_t len = 0;

			if (args[i] != CXX_TY_UNKNOWN && cxx_type_spell(c, args[i], buf, sizeof buf, &len))
				fprintf(stderr, " [%.*s]%s", (int)len, buf, lval[i] ? "&" : "");
			else
				fprintf(stderr, " [%s]", args[i] == CXX_TY_UNKNOWN ? "?" : "unspellable");
		}
		fputc('\n', stderr);
	}
	for (cxx_entity *e = set; e; e = e->next) {
		qty r;
		const ctype *sig = NULL;
		struct cand *k;

		if (e->kind == CXX_ENTITY_DEPENDENT) {
			if (!c->template_depth)
				c->stats.calls++, c->stats.why[CXX_WHY_DEPENDENT]++;
			cxx_et(c, CXX_TY_UNKNOWN, false);
			return;
		}
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
			if (d <= 0)
				continue;
			/* its parameters with the arguments bound */
			if (c->bound_sig != CXX_TY_UNKNOWN && ((AST_TYPE(c->ast, c->bound_sig)->flags & TF_VARIADIC) ||
								AST_TYPE(c->ast, c->bound_sig)->n >= nargs))
				sig = AST_TYPE(c->ast, c->bound_sig);	/* (f(...) too: an argument for the ellipsis ranks last) */
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
			sig = ft;
		}
		if (ncand == 24) {
			unknown = true;
			continue;
		}
		k = &cand[ncand];
		k->e = e, k->r = r, k->tmpl = e->kind == CXX_ENTITY_FUNCTION_TEMPLATE, k->ranked = sig != NULL && nargs <= 16;
		if (k->ranked) {
			bool fits = true;

			for (uint32_t a = 0; a < nargs && fits; a++) {
				/* (an argument for the ...: any) */
				k->rank[a] = a < sig->n ? (uint8_t)cxx_arg_rank(c, c->ast->params[sig->aux + a], args[a], lval[a], have_lval) : 5;	/* (...: last) */
				fits = k->rank[a] < 6;
			}
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
				if (!cand[a].tmpl && cand[b].tmpl)
					better = true;
				else if (cand[a].tmpl && cand[b].tmpl) {
					/* two templates: the more specialized (fewer parameters that are a bare T) */
					int ga = cxx_ftemplate_bare_params(c, cand[a].e, nargs), gb = cxx_ftemplate_bare_params(c, cand[b].e, nargs);

					if (ga >= 0 && gb >= 0 && ga != gb)
						better = ga < gb, worse = ga > gb;
					/* as specialized as each other: a declaration and its definition are one function, the
					   definition stands for it */
					else if (cxx_ftemplate_has_body(cand[a].e) != cxx_ftemplate_has_body(cand[b].e) && cand[a].r == cand[b].r)
						better = cxx_ftemplate_has_body(cand[a].e), worse = !better;
				}
				if (better || worse)
					;
				else if (cand[a].tmpl == cand[b].tmpl && obj == 1 && !cand[a].e->fn_const && cand[b].e->fn_const)
					better = true;
			}
			wins = better && !worse;
		}
		if (wins)
			best = (int)a;
	}
	if (ncand == 1 && !unknown)
		best = 0;
	/* (a template vcc could not deduce might be better than anything but a non-template that matches exactly) */
	if (best >= 0 && unknown) {
		bool exact = !cand[best].tmpl && cand[best].ranked;

		for (uint32_t n = 0; n < nargs && exact; n++)
			exact = cand[best].rank[n] <= 1;
		if (!exact)
			best = -1;
	}
	c->call_resolved = best >= 0;
	c->call_picked = best >= 0 ? cand[best].e : NULL;
	if (best >= 0)
		result = cand[best].r;
	else if (!viable || !same || unknown) {
		c->none_viable = !viable && !unknown;
		if (c->none_viable && !operator_call)
			c->einvalid = 1;	/* no function fits these arguments (a requirement fails) */
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
	if (result == CXX_TY_UNKNOWN && !c->template_depth && !c->instantiating && !c->unevaluated && getenv("VCC_CXX_WHY_TOP")) {
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
	if (best >= 0) {
		cxx_entity *called = cand[best].e;

		if (called->kind == CXX_ENTITY_VALUE)
			cxx_member_body_use(c, called);
		else if (called->kind == CXX_ENTITY_FUNCTION_TEMPLATE && !c->unevaluated && !c->tentative && nargs <= 16) {
			qty r, et = result;

			cxx_ftemplate_deduce(c, called, xargs, nxargs, args, lval, nargs, &r, true);
			result = et != CXX_TY_UNKNOWN ? et : r;
		}
	}
	(void)first;
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

	if (id <= 1 || obj == CXX_TY_UNKNOWN || nargs > 16)
		return false;
	cls = cxx_type_class_scope(c, QUNQ(obj));
	if (!cls)
		return false;
	/* (no candidate fits: the next place, in the end the built-in operator: &x, a = b, a, b) */
	set = cxx_lookup_in(c, cls, id, 0);
	if (set && set->kind != CXX_ENTITY_DEPENDENT) {
		c->call_obj = (QQ(obj) & Q_CONST) ? 2 : 1;	/* a member operator: on this object, const or not */
		cxx_call_type_impl(c, set, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, NULL, nargs, true);
		if (!c->none_viable)
			return true;
		/* obj(args) and no operator() of its class takes them: ill-formed (a conversion to a pointer
		   to function aside) */
		if (vintern_ents[id].len == 10 && !memcmp(vintern_ents[id].s, "operator()", 10) && !c->tentative) {
			bool known = true;

			for (uint32_t i = 0; i < nargs; i++)
				known &= args[i] != CXX_TY_UNKNOWN && !cxx_type_has_placeholder(c, args[i]);
			if (known)
				c->einvalid = 1;
		}
	}
	set = nargs <= 2 ? cxx_lookup(c, id) : NULL;	/* (operator() and operator[] are members) */
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

static int cxx_class_constructs(cxx *c, qty to, const qty *args, const uint8_t *lval, uint32_t n);
static bool cxx_class_converts(cxx *c, cxx_scope *cls, int depth);

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
	/* a class from or to an object of its own type: its copy or move constructor, or assignment operator */
	if (nt == 2 && (IS("__is_constructible") || IS("__is_assignable"))) {
		bool assign = IS("__is_assignable");
		qty to = t[0], from = t[1];
		const ctype *tt = AST_TYPE(c->ast, to), *ft = AST_TYPE(c->ast, from);
		bool rvalue = !(ft->kind == TY_PTR && ft->ext == 1);

		if (ft->kind == TY_PTR && ft->ext)
			from = ft->base;
		if (assign) {
			if (!(tt->kind == TY_PTR && tt->ext) || (QQ(tt->base) & Q_CONST)) {
				if (!(tt->kind == TY_PTR && tt->ext) && !cc_type_is_record(c->ast, to)) {
					*v = 0;	/* (a scalar rvalue is not assignable) */
					return true;
				}
				if (tt->kind == TY_PTR && tt->ext && !cc_type_is_record(c->ast, tt->base)) {
					*v = 0;	/* (a const scalar) */
					return true;
				}
			}
			if (tt->kind == TY_PTR && tt->ext)
				to = tt->base;
			if (!cc_type_is_record(c->ast, to) && !(QQ(to) & Q_CONST)) {
				int rank = cxx_arg_rank0(c, QUNQ(to), from, !rvalue, true);

				if (rank == 2)
					return false;
				*v = rank < 3;
				return true;
			}
		}
		if (cc_type_is_record(c->ast, to) && !(AST_TYPE_KIND(c->ast, to) == TY_PTR) && QUNQ(to) == QUNQ(from) &&
		    !(assign && (QQ(to) & Q_CONST))) {
			int r = cxx_class_special(c, QUNQ(to), assign, rvalue && !(QQ(from) & Q_CONST), 0);

			if (r < 0)
				return false;
			*v = r;
			return true;
		}
		if (assign)
			return false;
	}
	/* a class from arguments: its constructors decide */
	if (nt >= 1 && nt <= 2 && (IS("__is_constructible") || IS("__is_nothrow_constructible") || (nt == 2 && (IS("__is_convertible") ||
	    IS("__is_convertible_to") || IS("__is_nothrow_convertible"))))) {
		bool conv = !IS("__is_constructible") && !IS("__is_nothrow_constructible");
		bool nothrow = IS("__is_nothrow_constructible") || IS("__is_nothrow_convertible");
		qty to = conv ? t[1] : t[0], from = nt == 2 ? (conv ? t[0] : t[1]) : CXX_TY_UNKNOWN;
		uint8_t lv = 0;

		if (nt == 2 && AST_TYPE_KIND(c->ast, from) == TY_PTR && AST_TYPE(c->ast, from)->ext) {
			lv = AST_TYPE(c->ast, from)->ext == 1;
			from = AST_TYPE(c->ast, from)->base;
		}
		if (AST_TYPE_KIND(c->ast, to) == TY_PTR && AST_TYPE(c->ast, to)->ext && nt == 2 && !conv) {
			/* T&/const T&/T&& from a class object: a reference to it or to a base binds (or not) */
			const ctype *tt = AST_TYPE(c->ast, to);

			if (cc_type_is_record(c->ast, tt->base) && cc_type_is_record(c->ast, from) &&
			    (QUNQ(tt->base) == QUNQ(from) || (cxx_type_class_scope(c, QUNQ(from)) && cxx_derives(c, from, tt->base, 0)))) {
				bool cq = !(QQ(from) & Q_CONST) || (QQ(tt->base) & Q_CONST);

				*v = cq && (tt->ext == 1 ? lv || (QQ(tt->base) & Q_CONST) : !lv);
				return true;
			}
			/* T& (not const) from an unrelated class with no conversion function: nothing binds */
			if (cc_type_is_record(c->ast, tt->base) && cc_type_is_record(c->ast, from) && tt->ext == 1 && !(QQ(tt->base) & Q_CONST)) {
				cxx_scope *fs = cxx_type_class_scope(c, QUNQ(from));

				if (fs)
					cxx_type_require_complete(c, QUNQ(from));
				if (fs && !cxx_class_converts(c, fs, 0)) {
					*v = 0;
					return true;
				}
			}
		}
		if (cc_type_is_record(c->ast, to) && AST_TYPE_KIND(c->ast, to) != TY_PTR &&
		    (nt == 1 || (from != CXX_TY_UNKNOWN && AST_TYPE_KIND(c->ast, from) != TY_VOID))) {
			int r = cxx_class_constructs(c, to, &from, &lv, nt - 1);

			if (r < 0 || (r && nothrow))
				return false;
			*v = r;
			return true;
		}
		/* a class to something that is not one: its conversion functions (none: no) */
		if (nt == 2 && cc_type_is_record(c->ast, from) && !cc_type_is_record(c->ast, to) && AST_TYPE_KIND(c->ast, to) != TY_VOID &&
		    !(AST_TYPE_KIND(c->ast, to) == TY_PTR && AST_TYPE(c->ast, to)->ext)) {
			cxx_scope *fs = cxx_type_class_scope(c, QUNQ(from));

			if (fs)
				cxx_type_require_complete(c, QUNQ(from));
			if (!fs || fs->has_dependent_base || cxx_class_converts(c, fs, 0))
				return false;
			*v = 0;
			return true;
		}
	}
	/* how two types relate, where the answer does not need a class's constructors or conversion functions */
	if (nt == 2 && IS("__is_base_of")) {
		if (!cc_type_is_record(c->ast, t[0]) || !cc_type_is_record(c->ast, t[1])) {
			*v = 0;
			return true;
		}
		if (QUNQ(t[0]) == QUNQ(t[1])) {
			*v = 1;
			return true;
		}
		if (!cxx_type_class_scope(c, QUNQ(t[1])))
			return false;	/* (the derived class's bases: not known) */
		*v = cxx_derives(c, t[1], t[0], 0);
		return true;
	}
	if (nt == 2 && (IS("__is_convertible") || IS("__is_convertible_to") || IS("__is_nothrow_convertible") || IS("__is_constructible") ||
			IS("__is_nothrow_constructible") || IS("__is_trivially_constructible"))) {
		/* constructible is (To, From); convertible is (From, To) */
		bool ctor = IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_trivially_constructible");
		qty to = ctor ? t[0] : t[1], from = ctor ? t[1] : t[0];
		const ctype *ft = AST_TYPE(c->ast, from), *tt = AST_TYPE(c->ast, to);
		bool lv = ft->kind == TY_PTR && ft->ext == 1;
		int rank;

		if (ft->kind == TY_PTR && ft->ext)
			from = ft->base;
		if (tt->kind == TY_VOID || AST_TYPE_KIND(c->ast, from) == TY_VOID) {
			*v = !ctor && tt->kind == TY_VOID && AST_TYPE_KIND(c->ast, from) == TY_VOID;
			return true;
		}
		if (tt->kind == TY_ARRAY || tt->kind == TY_FUNC) {
			*v = 0;
			return true;
		}
		/* (an integer type is not a null pointer constant: only the literal 0 is) */
		if (tt->kind == TY_PTR && !tt->ext && AST_TYPE_KIND(c->ast, from) >= TY_BOOL && AST_TYPE_KIND(c->ast, from) <= TY_FLOAT128) {
			*v = 0;
			return true;
		}
		rank = cxx_arg_rank0(c, to, from, lv, true);
		if (rank == 2)
			return false;	/* (through a class: its constructors and conversion functions decide) */
		*v = rank < 3;
		return true;
	}
	if (nt != 1)
		return false;
	x = AST_TYPE(c->ast, t[0]);
	k = x->kind;
	ref = k == TY_PTR && x->ext;
	/* T(): scalars and pointers yes, references, void, functions and arrays of unknown bound no; a class's
	   default constructor decides for a class */
	if (IS("__is_constructible") || IS("__is_nothrow_constructible") || IS("__is_default_constructible")) {
		if ((k >= TY_BOOL && k <= TY_FLOAT128) || (k == TY_PTR && !x->ext) || k == TY_ENUM) {
			*v = 1;
			return true;
		}
		if (ref || k == TY_VOID || k == TY_FUNC) {
			*v = 0;
			return true;
		}
		return false;
	}
	/* what a type can do trivially: yes for scalars and arrays of them, and for a class vcc laid out as POD
	   (one that is not POD for layout may still be trivially copyable: not known) */
	{
		bool trivially = IS("__is_trivially_copyable") || IS("__is_trivial") || IS("__is_standard_layout") || IS("__is_pod") ||
				 IS("__is_trivially_destructible") || IS("__is_trivially_constructible") || IS("__has_trivial_destructor") ||
				 IS("__is_literal_type") || IS("__is_trivially_relocatable");
		bool never = IS("__is_empty") || IS("__is_polymorphic") || IS("__is_abstract");

		if (trivially || never) {
			qty e = t[0];
			bool pod, empty;

			while (AST_TYPE_KIND(c->ast, e) == TY_ARRAY)
				e = AST_TYPE(c->ast, e)->base;
			x = AST_TYPE(c->ast, e);
			if ((x->kind >= TY_BOOL && x->kind <= TY_FLOAT128) || (x->kind == TY_PTR && !x->ext) || x->kind == TY_ENUM) {
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
		cxx_scope *fs = cc_type_is_record(c->ast, t[0]) && k != TY_ARRAY ? cxx_type_class_scope(c, QUNQ(t[0])) : NULL;

		if (cc_type_is_record(c->ast, t[0]) && k != TY_ARRAY && !fs)
			return false;
		*v = fs && fs->is_final;
		return true;
	}
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

/* a range-for's element: *begin(r) for an array or a class with begin() (an lvalue's type; ADL begin: not yet) */
VPRIV bool cxx_range_element_type(cxx *c, qty r, qty *elem)
{
	const ctype *x;
	cxx_scope *cls;
	cxx_entity *set;
	qty it;

	if (r == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, r);
	if (x->kind == TY_ARRAY) {
		*elem = x->base;
		return true;
	}
	if (!(cls = cxx_type_class_scope(c, QUNQ(r))))
		return false;
	set = cxx_lookup_in(c, cls, vintern_cstr("begin"), 0);
	if (!set || set->kind == CXX_ENTITY_DEPENDENT)
		return false;
	c->call_obj = (QQ(r) & Q_CONST) ? 2 : 1;
	cxx_call_type(c, set, CXX_TY_UNKNOWN, NULL, NULL, 0);
	it = c->etype;
	if (it == CXX_TY_UNKNOWN)
		return false;
	x = AST_TYPE(c->ast, it);
	if (x->kind == TY_PTR && !x->ext) {
		*elem = x->base;
		return true;
	}
	/* an iterator class: its operator*() */
	if (!cxx_operator_call_type(c, vintern_cstr("operator*"), it, NULL, 0) || c->etype == CXX_TY_UNKNOWN)
		return false;
	*elem = c->etype;
	return true;
}

/* argument-dependent lookup: name in the namespace of each argument's class (the first that has it) */
VPRIV void cxx_adl_call_type(cxx *c, uint32_t name, const qty *args, const uint8_t *lval, uint32_t nargs)
{
	qty acopy[16];
	uint8_t lcopy[16];

	c->einvalid_unsure = 0;
	if (nargs > 16 || c->tentative) {
		c->einvalid_unsure = 1;
		return;
	}
	memcpy(acopy, args, nargs * sizeof *args), memcpy(lcopy, lval, nargs);
	for (uint32_t i = 0; i < nargs; i++) {
		qty t = acopy[i];
		cxx_scope *s;
		cxx_entity *set;

		if (t == CXX_TY_UNKNOWN)
			continue;
		if (AST_TYPE_KIND(c->ast, t) == TY_PTR)
			t = AST_TYPE(c->ast, t)->base;
		if (t == CXX_TY_UNKNOWN || !cc_type_is_record(c->ast, t) || !(s = cxx_type_class_scope(c, QUNQ(t))))
			continue;
		while (s && s->kind != CXX_SCOPE_NAMESPACE)
			s = s->parent;
		if (!s || !(set = cxx_lookup_in(c, s, name, 0)) || set->kind == CXX_ENTITY_DEPENDENT)
			continue;
		cxx_call_type(c, set, CXX_TY_UNKNOWN, acopy, lcopy, nargs);
		if (c->etype != CXX_TY_UNKNOWN)
			return;	/* (the caller decides what this means for the expression's validity) */
		if (!c->none_viable)
			c->einvalid_unsure = 1;	/* (candidates vcc could not decide between) */
	}
	cxx_et(c, CXX_TY_UNKNOWN, false);
}

/* std::nullptr_t: a type of its own (a class no source declares; it converts to any pointer) */
VPRIV qty cxx_nullptr_type(cxx *c)
{
	if (!c->nullptr_ty) {
		uint32_t tag = cc_tag_new(c->ast, TY_STRUCT, vintern_cstr("nullptr_t"), 0);

		c->nullptr_ty = QT(c->ast->tags[tag].ty, 0);
	}
	return c->nullptr_ty;
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
static int cxx_class_constructs(cxx *c, qty to, const qty *args, const uint8_t *lval, uint32_t n)
{
	cxx_scope *cls = cxx_type_class_scope(c, QUNQ(to)), *from_cls = NULL;
	cxx_entity *set, *own = NULL;
	bool same = false, derived = false;
	int r = -1;

	if (!cls)
		return -1;
	cxx_type_require_complete(c, QUNQ(to));
	if (cls->has_dependent_base)
		return -1;
	if (n == 1) {
		if (args[0] == CXX_TY_UNKNOWN)
			return -1;
		if (cc_type_is_record(c->ast, args[0]) && AST_TYPE_KIND(c->ast, args[0]) != TY_PTR) {
			from_cls = cxx_type_class_scope(c, QUNQ(args[0]));
			same = QUNQ(args[0]) == QUNQ(to);
			if (!same && from_cls) {
				cxx_type_require_complete(c, QUNQ(args[0]));
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
		qty et = c->etype;
		bool el = c->elval;
		cxx_entity *callee = c->ecallee, *picked;
		uint8_t invalid = c->einvalid, unsure = c->eunsure, missing = c->member_missing, adl = c->einvalid_unsure;
		bool resolved, none;

		cxx_call_type_impl(c, own, CXX_TY_UNKNOWN, NULL, UINT32_MAX, args, lval, n, true);
		resolved = c->call_resolved, none = c->none_viable, picked = c->call_picked;
		c->etype = et, c->elval = el, c->ecallee = callee;
		c->einvalid = invalid, c->eunsure = unsure, c->member_missing = missing, c->einvalid_unsure = adl;
		if (resolved)
			return !(picked && picked->kind == CXX_ENTITY_VALUE && picked->fn_deleted);
		if (!none && !same && !derived)
			return -1;
		r = 0;	/* (none of the declared ones; the implicit copy and move remain) */
	}
	if (same || derived) {
		int s = cxx_class_special(c, to, 0, !lval[0] && !(QQ(args[0]) & Q_CONST), 0);

		return s;
	}
	if (n == 1 && from_cls && cxx_class_converts(c, from_cls, 0))
		return -1;	/* (through the argument's conversion function, perhaps) */
	if (own)
		return r;
	/* no constructor declared: T() if its members allow it (taken to); T(a, ...) only for an aggregate
	   (C++20: parenthesized aggregate initialization), which is not decided here */
	if (!n) {
		for (uint32_t i = 0; i < cls->nfield_types; i++)
			if (cls->field_types[i] == CXX_TY_UNKNOWN)
				return -1;
			else if (AST_TYPE_KIND(c->ast, cls->field_types[i]) == TY_PTR && AST_TYPE(c->ast, cls->field_types[i])->ext)
				return 0;
			else if (cc_type_is_record(c->ast, cls->field_types[i]) && AST_TYPE_KIND(c->ast, cls->field_types[i]) != TY_ARRAY) {
				int f = cxx_class_constructs(c, cls->field_types[i], NULL, NULL, 0);

				if (f <= 0)
					return f;
			}
		for (uint32_t i = 0; i < cls->nused; i++) {
			cxx_scope *b = cls->used[i];

			if (b && b->kind == CXX_SCOPE_CLASS && b->owner && b->owner->ty != CXX_TY_UNKNOWN && cc_type_is_record(c->ast, b->owner->ty)) {
				int f = cxx_class_constructs(c, b->owner->ty, NULL, NULL, 0);

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
VPRIV int cxx_class_special(cxx *c, qty t, int what, bool rvalue, int depth)
{
	cxx_scope *cls = depth < 12 ? cxx_type_class_scope(c, QUNQ(t)) : NULL;
	cxx_entity *move = NULL, *copy = NULL, *pick;
	bool templates = false;
	int result = 1;

	if (getenv("VCC_CXX_NOSPECIAL"))
		return -1;
	if (!cls || cls->has_dependent_base)
		return -1;
	for (cxx_entity *e = cxx_lookup_in(c, cls, what ? vintern_cstr("operator=") : cxx_ctor_id(), 0); e; e = e->next) {
		const ctype *ft;
		qty p;

		if (e->member_of != cls)
			continue;	/* (a base's: not this class's own) */
		if (e->kind == CXX_ENTITY_FUNCTION_TEMPLATE) {
			templates = true;	/* (never a copy or move constructor; it may still be chosen) */
			continue;
		}
		if (e->kind != CXX_ENTITY_VALUE || e->ty == CXX_TY_UNKNOWN || AST_TYPE_KIND(c->ast, e->ty) != TY_FUNC)
			continue;
		ft = AST_TYPE(c->ast, e->ty);
		if (!ft->n || ft->n > 1u + e->ndefaults)
			continue;
		p = c->ast->params[ft->aux];
		if (AST_TYPE_KIND(c->ast, p) == TY_PTR && AST_TYPE(c->ast, p)->ext) {
			const ctype *pt = AST_TYPE(c->ast, p);

			if (QUNQ(pt->base) != QUNQ(t))
				continue;
			if (pt->ext == 2)
				move = e;
			else if (QQ(pt->base) & Q_CONST)
				copy = e;
			else if (!copy)
				copy = e;	/* X(X&): a copy constructor (from an lvalue that is not const) */
		} else if (QUNQ(p) == QUNQ(t) && what)
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
		qty m = cls->field_types[i];
		const ctype *mt;
		int r = 1;

		if (m == CXX_TY_UNKNOWN) {
			result = -1;
			continue;
		}
		while (AST_TYPE_KIND(c->ast, m) == TY_ARRAY)
			m = AST_TYPE(c->ast, m)->base;
		mt = AST_TYPE(c->ast, m);
		if (mt->kind == TY_PTR && mt->ext)
			r = !what;	/* a reference member: copied, not assigned */
		else if (what && (QQ(m) & Q_CONST))
			r = 0;		/* a const member: not assigned */
		else if (mt->kind == TY_STRUCT || mt->kind == TY_UNION)
			r = cxx_class_special(c, m, what, rvalue && !(QQ(m) & Q_CONST), depth + 1);
		if (!r)
			return 0;
		if (r < 0)
			result = -1;
	}
	return result;
}
