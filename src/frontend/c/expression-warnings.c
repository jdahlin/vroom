/* vcc front end: warnings of the expression parsers: division and shift, tautological compares, integer overflow, self-assignment */
#include "ccimpl.h"

/* -Wdivision-by-zero, -Wshift-count-*: l op r with a constant r */
VPRIV void runtime_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval cv;
	pp_span rr;

	if (c->dead || (op != LEX_TOKEN_SLASH && op != LEX_TOKEN_PERCENT && op != LEX_TOKEN_LESS_LESS && op != LEX_TOKEN_GREATER_GREATER) ||
	    !is_integer(c, N(l)->ty) || !is_integer(c, N(r)->ty) || !cc_eval(c, r, &cv) || cv.kind != CV_INT)
		return;
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT ? cv.i != 0 : cv.i >= 0 && cv.i < (i128)ty_size(c, promoted(c, l)) * 8)
		return;	/* no warning: no range (pp_tok_end) either */
	rr = rng(c, r);
	if (op == LEX_TOKEN_SLASH || op == LEX_TOKEN_PERCENT) {
		if (!cv.i && c->unevaluated)
			;	/* (sizeof, typeof: never runs) */
		else if (!cv.i && c->defer_rt) {
			if (c->ndefer < 8)
				c->defer[c->ndefer].loc = loc, c->defer[c->ndefer].r = rr, c->defer[c->ndefer++].rem = op == LEX_TOKEN_PERCENT;
		} else if (!cv.i && c->fn)	/* (clang's DiagRuntimeBehavior: at the end of the function) */
			rt_defer(c, "division-by-zero", NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
				 op == LEX_TOKEN_SLASH ? "division" : "remainder");
		else if (!cv.i)
			cc_warnx(c, "division-by-zero", true, NULL, loc, &rr, 1, NULL, 0, "%s by zero is undefined",
			      op == LEX_TOKEN_SLASH ? "division" : "remainder");
		return;
	}
	{
		qty pt = promoted(c, l);
		uint64_t w = ty_size(c, pt) * 8;
		vdiag *d = cv.i < 0 ? cc_warn_begin(c, "shift-count-negative", true, NULL, loc, "shift count is negative")
			   : cc_warn_begin(c, "shift-count-overflow", true, NULL, loc, "shift count >= width of type");

		if (!d)
			return;
		pp_diag_label(c->p, d, rr.b, rr.e, NULL);
		if (cv.i < 0)
			pp_diag_help(c->p, d, "a shift by a negative count is undefined");
		else
			pp_diag_help(c->p, d, "'%s' is %llu bits wide: a shift by %lld or more is undefined%s", tyname(c, pt),
				     (unsigned long long)w, (long long)w,
				     w < 64 && cv.i < 64 && op == LEX_TOKEN_LESS_LESS ? "; shift a 64-bit value (1ULL << n)" : "");
		rt_emit(c, d);
	}
}

/* clang's -Wconstant-logical-operand: 'x && 4' where '&' was likely meant (the right
   operand a constant other than 0 and 1, an integer non-bool left one, no enumerator) */
static __attribute__((cold, noinline)) void logic_const_warn(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc, i128 v)
{
	const char *o = op == LEX_TOKEN_AMPERSAND_AMPERSAND ? "&&" : "||", *bo = op == LEX_TOKEN_AMPERSAND_AMPERSAND ? "&" : "|";
	pp_span lr = rng(c, l), rr = rng(c, r);
	vdiag *d = cc_warn_begin(c, "constant-logical-operand", true, NULL, loc, "use of logical '%s' with constant operand", o), *n;

	(void)v;
	if (!d)
		return;
	pp_diag_label(c->p, d, rr.b, rr.e, NULL);
	n = pp_diag_note(c->p, d, loc, "use '%s' for a bitwise operation", bo);
	if (n)
		pp_diag_fix(c->p, n, loc, loc + 2, bo, VDA_MAYBE);
	if (op == LEX_TOKEN_AMPERSAND_AMPERSAND) {
		n = pp_diag_note(c->p, d, loc, "remove constant to silence this warning");
		if (n)
			pp_diag_fix(c->p, n, lr.e, rr.e, "", VDA_MAYBE);
	}
	pp_diag_emit(c->p, d);
}

/* clang's isSameComparisonOperand: the same variable, member chain or subscript once
   parentheses and implicit conversions are gone (an explicit cast is another operand) */
VPRIV uint32_t no_implicit(cc *c, uint32_t n)
{
	while (N(n)->op == N_CAST && !c->span[n].e)
		n = N(n)->a;
	return n;
}

/* the same object on both sides (clang's diagnoseTautologicalComparison:
   the same variable, member chain or constant-index element; implicit conversions and parentheses
   aside; nothing from a macro) */
VPRIV bool same_ref(cc *c, uint32_t a, uint32_t b, int depth)
{
	while (N(a)->op == N_CAST && !c->span[a].e && N(a)->a)
		a = N(a)->a;
	while (N(b)->op == N_CAST && !c->span[b].e && N(b)->a)
		b = N(b)->a;
	if (N(a)->op != N(b)->op || depth > 16)
		return false;
	switch (N(a)->op) {
	case N_VAR: {
		uint32_t nm = c->syms[N(a)->a].name;

		return N(a)->a == N(b)->a && c->syms[N(a)->a].kind == S_VAR && nm &&
		       pp_spells(c->p, N(a)->loc, idstr_(nm), (uint32_t)idlen_(nm)) &&
		       pp_spells(c->p, N(b)->loc, idstr_(nm), (uint32_t)idlen_(nm));
	}
	case N_MEMBER: {
		uint32_t x = N(a)->a, y = N(b)->a;

		if (N(a)->b != N(b)->b || N(a)->c != N(b)->c)
			return false;
		if (N(x)->op == N_DEREF && N(y)->op == N_DEREF)	/* p->m */
			x = N(x)->a, y = N(y)->a;
		return same_ref(c, x, y, depth + 1);
	}
	case N_INDEX: {
		uint32_t i = N(a)->b, j = N(b)->b;

		while (N(i)->op == N_CAST && !c->span[i].e && N(i)->a)
			i = N(i)->a;
		while (N(j)->op == N_CAST && !c->span[j].e && N(j)->a)
			j = N(j)->a;
		return N(i)->op == N_NUM && N(j)->op == N_NUM && c->ipool[N(i)->a] == c->ipool[N(j)->a] &&
		       same_ref(c, N(a)->a, N(b)->a, depth + 1);
	}
	default:
		return false;
	}
}

/* clang's -Wtautological-compare for x == x and friends (not floating: NaN) */
VPRIV __attribute__((cold, noinline)) void self_compare(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	bool t = op == LEX_TOKEN_EQUAL_EQUAL || op == LEX_TOKEN_LESS_EQUAL || op == LEX_TOKEN_GREATER_EQUAL;
	vdiag *d;

	(void)l, (void)r;
	if (c->dead && c->fn)
		return;
	d = cc_warn_begin(c, "tautological-compare", true, NULL, loc, "self-comparison always evaluates to %s", t ? "true" : "false");
	if (!d)
		return;
	pp_diag_help(c->p, d, "both sides are the same %s: was another one meant?",
		     N(no_implicit(c, l))->op == N_VAR ? "variable" : "object");
	pp_diag_emit(c->p, d);
}

/* an operand as clang prints it in these messages: v, s.f, p->f (appended at b[*k]) */
static void operand_name(cc *c, uint32_t n, char *b, size_t sz, size_t *k)
{
	const node *x = N(n = no_implicit(c, n));
	int l;
	const char *nm;

#define PUT(p, len) do { size_t q_ = (size_t)(len); if (*k + q_ < sz) memcpy(b + *k, (p), q_), *k += q_; } while (0)
	if (x->op == N_VAR) {
		nm = idname(c->syms[x->a].name, &l);
		PUT(nm, l);
	} else if (x->op == N_MEMBER) {
		const field *f = &c->tags[x->b].f[x->c];
		uint32_t base = no_implicit(c, x->a);

		if (N(base)->op == N_DEREF) {
			operand_name(c, N(base)->a, b, sz, k);
			PUT("->", 2);
		} else {
			operand_name(c, base, b, sz, k);
			PUT(".", 1);
		}
		if (f->name) {
			nm = idname(f->name, &l);
			PUT(nm, l);
		}
	}
#undef PUT
	b[*k < sz ? *k : sz - 1] = 0;
}

/* clang's -Wtautological-pointer-compare: an array, a function or &object against null */
VPRIV __attribute__((cold, noinline)) void null_compare(cc *c, int op, uint32_t x, uint32_t nul)
{
	uint32_t n = no_implicit(c, x), at;
	const node *y = N(n);
	const char *what;
	bool fn = false, eq = op == LEX_TOKEN_EQUAL_EQUAL;
	char nm[160];
	size_t nl = 0;
	pp_span xr, nr = rng(c, nul);
	vdiag *d;

	if (y->op == N_ADDR) {
		uint32_t in = no_implicit(c, y->a);

		if ((N(in)->op != N_VAR && N(in)->op != N_MEMBER) || is_func(c, N(in)->ty) ||
		    (N(in)->op == N_VAR && (c->syms[N(in)->a].flags & SF_WEAK)))
			return;
		what = "address of ", n = in;
	} else if (y->op == N_VAR && is_func(c, y->ty)) {
		if (c->syms[y->a].flags & SF_WEAK)
			return;
		what = "function ", fn = true;
	} else if ((y->op == N_VAR || y->op == N_MEMBER) && is_array(c, y->ty)) {
		if (y->op == N_VAR && (c->syms[y->a].flags & SF_WEAK))
			return;
		what = "array ";
	} else {
		return;
	}
	operand_name(c, n, nm, sizeof nm, &nl);
	if (!nl)
		return;
	xr = rng(c, n), at = N(n)->loc;
	if (N(n)->op == N_MEMBER)	/* clang: at the member's name, after '.' or '->' */
		at += N(no_implicit(c, N(n)->a))->op == N_DEREF ? 2 : 1;
	d = cc_warn_begin(c, "tautological-pointer-compare", true, NULL, at,
			  "comparison of %s'%s' %s to a null pointer is always %s", what, nm, eq ? "equal" : "not equal",
			  eq ? "false" : "true");
	if (d) {
		pp_diag_label(c->p, d, xr.b, xr.e, NULL);
		pp_diag_label(c->p, d, nr.b, nr.e, NULL);
		if (fn) {
			const ctype *ft = T(c, y->ty);
			vdiag *k = pp_diag_note(c->p, d, xr.b, "prefix with the address-of operator to silence this warning");

			if (k)
				pp_diag_fix(c->p, k, xr.b, xr.b, "&", VDA_MAYBE);
			if (!ft->n && !(ft->flags & TF_VARIADIC) && !is_void(c, ft->base)) {
				k = pp_diag_note(c->p, d, xr.b, "suffix with parentheses to turn this into a function call");
				if (k)
					pp_diag_fix(c->p, k, xr.e, xr.e, "()", VDA_MAYBE);
			}
		} else {
			pp_diag_help(c->p, d, "%s'%s' is never null", what, nm);
		}
		pp_diag_emit(c->p, d);
	}
}


/* clang's -Wtautological-constant-out-of-range-compare: a constant the other operand's own type
   (implicit conversions stripped) cannot hold, compared in the comparison's type */
VPRIV __attribute__((cold, noinline)) void range_compare(cc *c, uint32_t n, int nk, uint32_t loc)
{
	const node *x = N(n);
	int op = x->aux;
	uint32_t k = nk ? x->b : x->a, o = nk ? x->a : x->b, os = no_implicit(c, o);
	qty ot = N(os)->ty;
	cval v, ov;
	i128 lo, hi;
	uint64_t w;
	bool above, res;
	char vs[48];
	pp_span orr = rng(c, os), kr = rng(c, no_implicit(c, k));
	vdiag *d;

	if (!is_integer(c, ot) || (cc_eval(c, os, &ov) && ov.kind == CV_INT) || !cc_eval(c, k, &v) || v.kind != CV_INT)
		return;
	w = ty_size(c, ot) * 8;
	if (is_signed(c, ot) && !is_signed(c, N(k)->ty) && KIND(c, ot) != TY_BOOL)
		return;	/* a signed operand in an unsigned comparison: not one interval (clang) */
	if (KIND(c, ot) == TY_BOOL)
		lo = 0, hi = 1;
	else if (w >= 64 || w >= ty_size(c, N(k)->ty) * 8)
		return;	/* as wide as the comparison: every converted constant fits */
	else if (is_signed(c, ot))
		lo = -((i128)1 << (w - 1)), hi = ((i128)1 << (w - 1)) - 1;
	else
		lo = 0, hi = ((i128)1 << w) - 1;
	if (v.i >= lo && v.i <= hi)
		return;
	above = v.i > hi;
	if (!nk)	/* constant on the left: K op X is X op' K */
		op = op == LEX_TOKEN_LESS ? LEX_TOKEN_GREATER : op == LEX_TOKEN_GREATER ? LEX_TOKEN_LESS : op == LEX_TOKEN_LESS_EQUAL ? LEX_TOKEN_GREATER_EQUAL : op == LEX_TOKEN_GREATER_EQUAL ? LEX_TOKEN_LESS_EQUAL : op;
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: res = false; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: res = true; break;
	case LEX_TOKEN_LESS: case LEX_TOKEN_LESS_EQUAL: res = above; break;
	default: res = !above; break;
	}
	i128s_(vs, sizeof vs, v.i);
	d = cc_warn_begin(c, "tautological-constant-out-of-range-compare", true, NULL, loc,
			  "result of comparison of constant %s with expression of type '%s' is always %s", vs, tyname(c, ot),
			  res ? "true" : "false");
	if (!d)
		return;
	if (nk)
		pp_diag_label(c->p, d, orr.b, orr.e, NULL), pp_diag_label(c->p, d, kr.b, kr.e, NULL);
	else
		pp_diag_label(c->p, d, kr.b, kr.e, NULL), pp_diag_label(c->p, d, orr.b, orr.e, NULL);
	{
		char ls[48], hs[48];

		i128s_(ls, sizeof ls, lo), i128s_(hs, sizeof hs, hi);
		pp_diag_help(c->p, d, "'%s' holds %s to %s%s", tyname(c, ot), ls, hs, char_sign(c, ot));
	}
	rt_emit(c, d);
}


VPRIV void logic_const(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	cval v;

	if (!is_integer(c, N(r)->ty) || !is_integer(c, N(l)->ty) || KIND(c, N(l)->ty) == TY_BOOL ||
	    !cc_eval(c, r, &v) || v.kind != CV_INT || v.i == 0 || v.i == 1)
		return;
	if (N(r)->op == N_NUM && (N(r)->aux & NUM_ENUM))
		return;
	if (N(l)->op == N_NUM && (N(l)->aux & NUM_ENUM)) {
		cval lv;

		if (cc_eval(c, l, &lv) && lv.kind == CV_INT && lv.i != 0 && lv.i != 1)
			return;
	}
	logic_const_warn(c, op, l, r, loc, v.i);
}


/* -Winteger-overflow: n, a signed integer +, -, * or unary - of constants,
   whose exact value does not fit its type.  Once per expression (NF_OVF). */
VPRIV void ovf_warn(cc *c, int op, uint32_t n, uint32_t loc, uint32_t sub0, uint32_t sub1)
{
	cval a, b;
	i128 v;
	int bits;
	qty t = N(n)->ty;

	if ((N(sub0)->flags & NF_OVF) || (sub1 && (N(sub1)->flags & NF_OVF))) {
		N(n)->flags |= NF_OVF;
		return;
	}
	if (c->dead || c->unevaluated || !is_integer(c, t) || !is_signed(c, t) || (bits = (int)ty_size(c, t) * 8) > 64 ||
	    !cc_eval(c, sub0, &a) || a.kind != CV_INT || (sub1 && (!cc_eval(c, sub1, &b) || b.kind != CV_INT)))
		return;
	if (!sub1)
		v = -a.i;
	else if (op == LEX_TOKEN_PLUS)
		v = a.i + b.i;
	else if (op == LEX_TOKEN_MINUS)
		v = a.i - b.i;
	else
		v = a.i * b.i;
	if (v >= -((i128)1 << (bits - 1)) && v < ((i128)1 << (bits - 1)))
		return;
	N(n)->flags |= NF_OVF;
	{
		char d[48], o[64];
		int64_t w = bits == 64 ? (int64_t)(uint64_t)v : (int64_t)(int32_t)(uint32_t)v;
		uint64_t m = w < 0 ? -(uint64_t)w : (uint64_t)w;
		int nd = 0, k = 0;
		pp_span r = rng(c, n);

		do
			d[nd++] = (char)('0' + m % 10), m /= 10;
		while (m);
		if (w < 0)
			o[k++] = '-';
		while (nd) {
			o[k++] = d[--nd];
			if (nd && nd % 3 == 0)
				o[k++] = '\'';
		}
		o[k] = 0;
		cc_warnx(c, "integer-overflow", true, NULL, loc, &r, 1, NULL, 0,
			 "overflow in expression; result is %s with type '%s'", o, tyname(c, t));
	}
}

/* -Wself-assign: x = x (parentheses and implicit conversions aside), not volatile */
VPRIV void self_assign(cc *c, uint32_t l, uint32_t r, uint32_t loc)
{
	while (N(r)->op == N_CAST && !c->span[r].e && N(r)->a)
		r = N(r)->a;
	if (N(l)->op != N_VAR || N(r)->op != N_VAR || N(l)->a != N(r)->a || (QQ(N(l)->ty) & Q_VOLATILE) ||
	    c->syms[N(l)->a].kind != S_VAR)
		return;
	{
		uint32_t nm = c->syms[N(l)->a].name;

		if (!pp_spells(c->p, N(l)->loc, idstr_(nm), (uint32_t)idlen_(nm)) ||
		    !pp_spells(c->p, N(r)->loc, idstr_(nm), (uint32_t)idlen_(nm)))
			return;	/* (clang: not from a macro) */
	}
	{
		pp_span rr[2] = { { N(l)->loc, pp_tok_end(c->p, N(l)->loc) }, { N(r)->loc, pp_tok_end(c->p, N(r)->loc) } };

		cc_warnx(c, "self-assign", false, w_selfassign, loc, rr, 2, NULL, 0,
			 "explicitly assigning value of variable of type '%s' to itself", tyname(c, N(l)->ty));
	}
}
