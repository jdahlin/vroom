/* vcc code generator: conditions and range folding */
#include "../../../emit/emitimpl.h"

/* ---------------- conditions ---------------- */

/* jump to l if n's truth == jump_if; returns the constant truth or -1 */
/* e >> 0, e << 0: e */
static uint32_t strip_shift0(cc *c, uint32_t n)
{
	while (N(n)->op == N_BINARY && (N(n)->aux == T_LESSLESS || N(n)->aux == T_GREATERGREATER) &&
	       N(N(n)->b)->op == N_NUM && c->ipool[N(N(n)->b)->a] == 0 && QUNQ(N(n)->ty) == QUNQ(N(N(n)->a)->ty))
		n = N(n)->a;
	return n;
}

/* the same value, without side effects (integer or pointer, nothing volatile) */
static bool same_pure(cc *c, uint32_t a, uint32_t b)
{
	const node *x, *y;

	a = strip_shift0(c, a), b = strip_shift0(c, b);
	x = N(a), y = N(b);
	if (x->op != y->op || QUNQ(x->ty) != QUNQ(y->ty) || (QQ(x->ty) | QQ(y->ty)) & Q_VOLATILE ||
	    !(is_integer(c, x->ty) || is_ptr(c, x->ty)))
		return false;
	switch (x->op) {
	case N_VAR:
		return x->a == y->a && c->syms[x->a].kind == S_VAR && !(QQ(c->syms[x->a].ty) & Q_VOLATILE);
	case N_NUM:
		return c->ipool[x->a] == c->ipool[y->a];
	case N_CAST:
		return x->aux == y->aux && same_pure(c, x->a, y->a);
	case N_BINARY:
		return x->aux == y->aux && !em_is_cmp(x->aux) && x->aux != T_SLASH && x->aux != T_PERCENT &&
		       same_pure(c, x->a, y->a) && same_pure(c, x->b, y->b);
	}
	return false;
}

/* a comparison of two integers as the outcomes it holds for (1 <, 2 ==, 4 >) and its operands */
static int cmp_mask(cc *c, uint32_t n, uint32_t *l, uint32_t *r)
{
	const node *x = N(n);
	int m;

	if (x->op != N_BINARY || !em_is_cmp(x->aux) || is_flt(c, N(x->a)->ty) || is_wide(c, N(x->a)->ty))
		return 0;
	switch (x->aux) {
	case T_LESS: m = 1; break;
	case T_EQUALEQUAL: m = 2; break;
	case T_LESSEQ: m = 3; break;
	case T_GREATER: m = 4; break;
	case T_EXCLAIMEQ: m = 5; break;
	default: m = 6; break;
	}
	*l = x->a, *r = x->b;
	return m;
}

/* the values of an integer type */
static bool trange(cc *c, qty t, i128 *lo, i128 *hi)
{
	uint64_t sz;

	if (!is_integer(c, t) || is_wide(c, t) || KIND(c, t) == TY_BOOL)
		return false;
	sz = tsize(c, t) * 8;
	if (is_signed(c, t))
		*lo = -((i128)1 << (sz - 1)), *hi = ((i128)1 << (sz - 1)) - 1;
	else
		*lo = 0, *hi = ((i128)1 << sz) - 1;
	return true;
}

/* an interval holding every value of pure integer expression n, with variable s in [slo, shi] */
static bool irange(cc *c, uint32_t n, uint32_t s, i128 slo, i128 shi, i128 *lo, i128 *hi)
{
	const node *x = N(n);
	i128 tlo, thi, alo, ahi, blo, bhi;

	cval cv;

	if (!trange(c, x->ty, &tlo, &thi) || (QQ(x->ty) & Q_VOLATILE))
		return false;
	if (x->op != N_VAR && cc_eval(c, n, &cv) && cv.kind == CV_INT) {
		*lo = *hi = em_trunc_to(c, cv.i, x->ty);
		return true;
	}
	switch (x->op) {
	case N_NUM:
		*lo = *hi = em_trunc_to(c, c->ipool[x->a], x->ty);
		return true;
	case N_VAR:
		if (x->a == s)
			*lo = slo, *hi = shi;
		else
			*lo = tlo, *hi = thi;
		return true;
	case N_CAST:
		if (!irange(c, x->a, s, slo, shi, &alo, &ahi))
			return false;
		break;
	case N_BINARY:
		if ((x->aux != T_PLUS && x->aux != T_MINUS) || !irange(c, x->a, s, slo, shi, &alo, &ahi) ||
		    !irange(c, x->b, s, slo, shi, &blo, &bhi))
			return false;
		if (x->aux == T_PLUS)
			alo += blo, ahi += bhi;
		else
			alo -= bhi, ahi -= blo;
		break;
	default:
		return false;
	}
	/* out of the type's range: anything (wraps) */
	if (alo < tlo || ahi > thi)
		alo = tlo, ahi = thi;
	*lo = alo, *hi = ahi;
	return true;
}

/* comparison n with s in [slo, shi]: 0 or 1 when the intervals decide it, -1 */
static int irange_cmp(cc *c, uint32_t n, uint32_t s, i128 slo, i128 shi)
{
	const node *x = N(n);
	i128 alo, ahi, blo, bhi;

	if (x->op != N_BINARY || !em_is_cmp(x->aux) || !irange(c, x->a, s, slo, shi, &alo, &ahi) ||
	    !irange(c, x->b, s, slo, shi, &blo, &bhi))
		return -1;
	switch (x->aux) {
	case T_LESS: return ahi < blo ? 1 : alo >= bhi ? 0 : -1;
	case T_LESSEQ: return ahi <= blo ? 1 : alo > bhi ? 0 : -1;
	case T_GREATER: return alo > bhi ? 1 : ahi <= blo ? 0 : -1;
	case T_GREATEREQ: return alo >= bhi ? 1 : ahi < blo ? 0 : -1;
	case T_EQUALEQUAL: return alo == ahi && blo == bhi && alo == blo ? 1 : ahi < blo || bhi < alo ? 0 : -1;
	case T_EXCLAIMEQ: return alo == ahi && blo == bhi && alo == blo ? 0 : ahi < blo || bhi < alo ? 1 : -1;
	}
	return -1;
}

/* the range of local s where comparison n (s OP constant) has truth t */
static bool cmp_var_range(cc *c, uint32_t n, bool t, uint32_t *s, i128 *lo, i128 *hi)
{
	const node *x = N(n);
	int op;
	i128 k;

	if (x->op != N_BINARY || !em_is_cmp(x->aux) || N(x->a)->op != N_VAR || N(x->b)->op != N_NUM ||
	    !trange(c, N(x->a)->ty, lo, hi) || (QQ(N(x->a)->ty) & Q_VOLATILE) || c->syms[N(x->a)->a].kind != S_VAR)
		return false;
	op = x->aux, k = em_trunc_to(c, c->ipool[N(x->b)->a], N(x->a)->ty);
	if (!t)
		op = op == T_LESS ? T_GREATEREQ : op == T_LESSEQ ? T_GREATER : op == T_GREATER ? T_LESSEQ :
		     op == T_GREATEREQ ? T_LESS : op == T_EQUALEQUAL ? T_EXCLAIMEQ : T_EQUALEQUAL;
	*s = N(x->a)->a;
	switch (op) {
	case T_LESS: *hi = k - 1; break;
	case T_LESSEQ: *hi = k; break;
	case T_GREATER: *lo = k + 1; break;
	case T_GREATEREQ: *lo = k; break;
	case T_EQUALEQUAL: *lo = *hi = k; break;
	default: return false;
	}
	return *lo <= *hi;
}

/* a condition known from its form (x != x, x < y && x > y, ...): 0 or 1; -1 unknown */
/* a floating local whose every write has fact f (FF_NONNEG / FF_ZERO), or a +-0.0 constant for FF_ZERO */
static bool ffact(ecx *e, uint32_t n, uint8_t f)
{
	cc *c = e->c;
	const node *x = N(n);
	cval cv;

	if (x->op == N_VAR && is_local_var(c, x->a) && !(QQ(x->ty) & Q_VOLATILE)) {
		uint8_t g = e->E->ffacts[x->a];

		return (g & (FF_SEEN | FF_BAD)) == FF_SEEN && (g & f) == f;
	}
	return f == FF_ZERO && is_flt(c, x->ty) && cc_eval(c, n, &cv) && cv.kind == CV_FLT && (cv.ld ? !(cv.q << 1) : cv.f == 0);
}

VPRIV int cond_fold(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t l1, r1, l2, r2, s;
	int m1, m2, m;
	i128 lo, hi;

	/* a || b with b true wherever a is false (a && b: b false wherever a is true) */
	if ((x->op == N_LOGAND || x->op == N_LOGOR) && cmp_var_range(c, x->a, x->op == N_LOGAND, &s, &lo, &hi)) {
		int k = irange_cmp(c, x->b, s, lo, hi);

		if (x->op == N_LOGOR && k == 1)
			return 1;
		if (x->op == N_LOGAND && k == 0)
			return 0;
	}
	if (x->op == N_UNARY && x->aux == T_EXCLAIM) {
		int k = cond_fold(e, x->a);

		return k < 0 ? -1 : !k;
	}
	if (x->op == N_LOGAND || x->op == N_LOGOR) {
		m1 = cmp_mask(c, x->a, &l1, &r1);
		m2 = cmp_mask(c, x->b, &l2, &r2);
		if (!m1 || !m2)
			return -1;
		if (same_pure(c, l1, r2) && same_pure(c, r1, l2))	/* swapped: < and > trade places */
			m2 = (m2 & 2) | (m2 & 1) << 2 | (m2 & 4) >> 2;
		else if (!same_pure(c, l1, l2) || !same_pure(c, r1, r2))
			return -1;
		m = x->op == N_LOGAND ? m1 & m2 : m1 | m2;
		return m == 0 ? 0 : m == 7 ? 1 : -1;
	}
	/* fabs(x) < 0.0 (through locals): false, NaN included */
	if (x->op == N_BINARY && (x->aux == T_LESS || x->aux == T_GREATER) && is_flt(c, N(x->a)->ty) &&
	    !is_ld(c, N(x->a)->ty) && ffact(e, x->aux == T_LESS ? x->a : x->b, FF_NONNEG) &&
	    ffact(e, x->aux == T_LESS ? x->b : x->a, FF_ZERO))
		return 0;
	m = cmp_mask(c, n, &l1, &r1);
	if (m && same_pure(c, l1, r1))
		return (m & 2) != 0;
	return -1;
}

/* ---------------- -O3: range checks and flag chains ---------------- */

/* n as an integer constant (N_NUM, possibly converted) */
VPRIV bool num_of(cc *c, uint32_t n, i128 *v)
{
	const node *x = N(n);

	if (x->op == N_NUM && is_integer(c, x->ty)) {
		*v = c->ipool[x->a];
		return true;
	}
	if ((x->op == N_UNARY || x->op == N_CAST) && is_integer(c, x->ty) && !is_wide(c, x->ty)) {
		cval cv;

		if (cc_eval(c, n, &cv) && cv.kind == CV_INT) {
			*v = em_trunc_to(c, cv.i, x->ty);
			return true;
		}
	}
	return false;
}

/*
 * An operand that can be evaluated whether or not it is needed: no side
 * effects, no faults, and code that leaves the flags alone (loads of
 * locals, constants, integer conversions).
 */
VPRIV bool cheap_arm(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);

	if ((!is_integer(c, x->ty) && !is_ptr(c, x->ty)) || is_wide(c, x->ty) || (QQ(x->ty) & Q_VOLATILE))
		return false;
	switch (x->op) {
	case N_NUM:
		return true;
	case N_VAR:
		return is_local_var(c, x->a) && !(QQ(c->syms[x->a].ty) & Q_VOLATILE);
	case N_CAST:
		return (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_P2P || x->aux == CK_I2P || x->aux == CK_P2I) &&
		       cheap_arm(e, x->a);
	case N_UNARY: {
		i128 v;

		return num_of(c, n, &v);	/* -k */
	}
	default:
		return false;
	}
}

/* the same side-effect-free value: a local variable, through the same conversions */
static bool same_simple(cc *c, uint32_t a, uint32_t b)
{
	const node *x = N(a), *y = N(b);

	if (x->op != y->op || x->ty != y->ty || (QQ(x->ty) & Q_VOLATILE))
		return false;
	if (x->op == N_VAR)
		return x->a == y->a && is_local_var(c, x->a) && !(QQ(c->syms[x->a].ty) & Q_VOLATILE);
	if (x->op == N_CAST)
		return x->aux == y->aux && x->aux == CK_INT && same_simple(c, x->a, y->a);
	return false;
}

/* one bound of a range check: v >= k (lower) or v <= k (upper), any spelling; returns the v node or 0 */
static uint32_t range_bound(cc *c, uint32_t n, bool *lower, i128 *k)
{
	const node *x = N(n);
	qty lt;
	int op;
	uint32_t v;
	i128 m, lo, hi;

	if (x->op != N_BINARY || !em_is_cmp(x->aux) || x->aux == T_EQUALEQUAL || x->aux == T_EXCLAIMEQ)
		return 0;
	lt = N(x->a)->ty;
	if (!is_integer(c, lt) || is_wide(c, lt) || tsize(c, lt) < 4)
		return 0;
	op = x->aux;
	if (num_of(c, x->b, &m)) {
		v = x->a;
	} else if (num_of(c, x->a, &m)) {
		v = x->b;	/* k op v: mirror */
		op = op == T_LESS ? T_GREATER : op == T_GREATER ? T_LESS : op == T_LESSEQ ? T_GREATEREQ : T_LESSEQ;
	} else {
		return 0;
	}
	m = em_trunc_to(c, m, lt);
	if (is_signed(c, lt)) {
		lo = tsize(c, lt) == 8 ? (i128)INT64_MIN : (i128)INT32_MIN;
		hi = tsize(c, lt) == 8 ? (i128)INT64_MAX : (i128)INT32_MAX;
	} else {
		lo = 0;
		hi = tsize(c, lt) == 8 ? (i128)UINT64_MAX : (i128)UINT32_MAX;
	}
	switch (op) {
	case T_GREATEREQ: *lower = true; *k = m; break;
	case T_GREATER: *lower = true; *k = m + 1; break;
	case T_LESSEQ: *lower = false; *k = m; break;
	default: *lower = false; *k = m - 1; break;
	}
	return *k < lo || *k > hi ? 0 : v;
}

/*
 * v >= lo && v <= hi (any order and spelling, v a local) as one unsigned
 * compare (v - lo) <= (hi - lo); v < lo || v > hi as its negation.
 * Returns the condition that is true when the && or || is (the flags
 * set), FL_TRUE/FL_FALSE when v is known, or -1.
 */
VPRIV int range_check(ecx *e, uint32_t n)
{
	cc *c = e->c;
	const node *x = N(n);
	uint32_t va, vb;
	bool la, lb;
	i128 ka, kb, lo, hi;
	qty lt;
	int cx, r;
	val v;

	if (!e->E->opt3 || (x->op != N_LOGAND && x->op != N_LOGOR))
		return -1;
	if (!(va = range_bound(c, x->a, &la, &ka)) || !(vb = range_bound(c, x->b, &lb, &kb)) || la == lb ||
	    !same_simple(c, va, vb))
		return -1;
	lt = N(va)->ty;		/* the type both compares are done in */
	if (x->op == N_LOGAND) {
		lo = la ? ka : kb, hi = la ? kb : ka;
	} else {
		/* v < lo || v > hi: outside the range */
		lo = (la ? kb : ka) + 1, hi = (la ? ka : kb) - 1;
	}
	if (lo > hi)
		return -1;
	cx = X(c, lt);
	v = gen(e, va);
	if (v.k == V_CONST) {
		/* a known value: no code (what it guards may not exist) */
		i128 k = em_trunc_to(c, v.i, lt);
		bool in = k >= lo && k <= hi;

		return (x->op == N_LOGAND ? in : !in) ? FL_TRUE : FL_FALSE;
	}
	r = toreg(e, &v);
	if (lo)
		opi(e, I_SUBI, cx, r, r, (int64_t)lo);
	if (hi - lo <= 4095) {
		opi(e, I_SUBSI, cx, ZR, r, (int64_t)(hi - lo));
	} else {
		movi(e, cx, R_X16, (int64_t)(hi - lo));
		op3(e, I_SUBS, cx, ZR, r, R_X16);
	}
	val_free(e, &v);
	return x->op == N_LOGAND ? C_LS : C_HI;
}

/*
 * && and || of integer compares as one flag chain (cmp; ccmp ...; then
 * cset or b.cond).  The chain is left-deep: (A op B) op C ..., each
 * operand after the first a compare of cheap values (cheap_arm), since it
 * is evaluated whatever the ones before decided.
 */
static bool flag_leaf(ecx *e, uint32_t n, bool first)
{
	cc *c = e->c;
	const node *x = N(n);
	qty lt;

	if (x->op == N_UNARY && x->aux == T_EXCLAIM)
		return flag_leaf(e, x->a, first);
	if (x->op != N_BINARY || !em_is_cmp(x->aux))
		return false;
	lt = N(x->a)->ty;
	if ((!is_integer(c, lt) && !is_ptr(c, lt)) || is_wide(c, lt) || is_vec(c, x->ty) || is_vec(c, lt))
		return false;
	return first || (cheap_arm(e, x->a) && cheap_arm(e, x->b));
}

VPRIV bool flaggable(ecx *e, uint32_t n, bool first)
{
	cc *c = e->c;
	const node *x = N(n);

	if (!e->E->opt3)
		return false;
	if (x->op == N_LOGAND || x->op == N_LOGOR)
		return flaggable(e, x->a, first) && flag_leaf(e, x->b, false);
	return flag_leaf(e, n, first);
}

/* NZCV values that make each condition true */
static const uint8_t nzcv_true[] = {
	[C_EQ] = 4, [C_NE] = 0, [C_HS] = 2, [C_LO] = 0, [C_MI] = 8, [C_PL] = 0, [C_VS] = 1, [C_VC] = 0,
	[C_HI] = 2, [C_LS] = 4, [C_GE] = 0, [C_LT] = 8, [C_GT] = 0, [C_LE] = 4,
};

/* a compare leaf: cmp (first: cin < 0), or ccmp under cin (&&: when cin holds; ||: when it does not) */
static int flag_cmp(ecx *e, uint32_t n, int cin, bool isand)
{
	cc *c = e->c;
	const node *x = N(n);
	bool inv = false, sg;
	qty lt;
	val a, b;
	int cond, x64;

	while (x->op == N_UNARY) {
		inv = !inv;
		n = x->a, x = N(n);
	}
	lt = N(x->a)->ty;
	sg = !is_ptr(c, lt) && is_signed(c, lt);
	x64 = X(c, lt);
	a = gen(e, x->a);
	b = gen(e, x->b);
	cond = cmp_cond(x->aux, sg) ^ inv;
	if ((a.k == V_CONST || a.k == V_ADDR) && (b.k == V_CONST || b.k == V_ADDR)) {
		/* folds as gen_binary folds it, else compared at run time */
		int t = -1;

		if (a.k == V_CONST && b.k == V_CONST) {
			bool ok;
			val f = fold_int_bin(e, x->aux, is_ptr(c, lt) ? prim(TY_ULONG) : lt, x->ty, a.i, b.i, &ok);

			if (ok)
				t = f.i != 0;
		} else if (a.k == V_ADDR && b.k == V_ADDR && a.ref == b.ref) {
			bool ok;
			val f = fold_int_bin(e, x->aux, prim(TY_LONG), x->ty, a.i, b.i, &ok);

			if (ok)
				t = f.i != 0;
		}
		if (t >= 0)
			return (t ^ inv) ? FL_TRUE : FL_FALSE;
	}
	if (cin < 0) {
		emit_cmp(e, x->aux, x64, sg, &a, &b);
	} else {
		int64_t k = 0;
		bool imm = false;
		int ra = toreg(e, &a), rb = 0;

		if (b.k == V_CONST) {
			k = x64 ? (int64_t)b.i : (int32_t)b.i;
			imm = k >= -31 && k <= 31;
		}
		if (!imm)
			rb = toreg(e, &b);
		if (e->reach) {
			ins *i = put(e, I_CCMP);

			i->sz = (uint8_t)x64, i->rn = (uint8_t)ra, i->rm = (uint8_t)rb, i->sh = imm;
			i->imm = imm ? k : 0;
			i->cond = (uint8_t)(isand ? cin : cin ^ 1);
			i->ra = nzcv_true[isand ? cond ^ 1 : cond];
		}
	}
	val_free(e, &a);
	val_free(e, &b);
	return cond;
}

/* the chain at n (flaggable): returns the condition that holds when n is true, or FL_TRUE/FL_FALSE */
VPRIV int gen_flags(ecx *e, uint32_t n, bool first)
{
	cc *c = e->c;
	const node *x = N(n);

	if (x->op == N_LOGAND || x->op == N_LOGOR) {
		bool isand = x->op == N_LOGAND;
		int rc = first ? range_check(e, n) : -1, rb;

		if (rc >= 0)
			return rc;
		rc = gen_flags(e, x->a, first);
		if (rc == (isand ? FL_FALSE : FL_TRUE))
			return rc;	/* decided: the rest is cheap, nothing to evaluate */
		if (rc >= FL_TRUE)
			return flag_cmp(e, x->b, -1, false);
		rb = flag_cmp(e, x->b, rc, isand);
		if (rb >= FL_TRUE)
			return rb == (isand ? FL_TRUE : FL_FALSE) ? rc : rb;
		return rb;
	}
	return flag_cmp(e, n, -1, false);
}

VPRIV void gen_cond(ecx *e, uint32_t n, bool jump_if, uint32_t l)
{
	cc *c = e->c;
	const node *x = N(n);
	int k = cond_fold(e, n);

	if (k >= 0) {
		if (k == (int)jump_if)
			jump(e, l);
		return;
	}
	switch (x->op) {
	case N_LOGAND:
	case N_LOGOR: {
		bool isand = x->op == N_LOGAND;
		int rc = flaggable(e, n, true) ? gen_flags(e, n, true) : range_check(e, n);

		if (rc >= FL_TRUE) {
			if ((rc == FL_TRUE) == jump_if)
				jump(e, l);
			return;
		}
		if (rc >= 0) {
			bcond(e, jump_if ? rc : rc ^ 1, l);
			return;
		}
		if (isand != jump_if) {
			/* && jumping on false / || jumping on true: both operands jump to l */
			gen_cond(e, x->a, jump_if, l);
			gen_cond(e, x->b, jump_if, l);
		} else {
			uint32_t skip = newlabel(e);

			gen_cond(e, x->a, !jump_if, skip);
			gen_cond(e, x->b, jump_if, l);
			deflabel(e, skip);
		}
		return;
	}
	case N_UNARY:
		if (x->aux == T_EXCLAIM) {
			gen_cond(e, x->a, !jump_if, l);
			return;
		}
		break;
	case N_CAST:
		if (x->aux == CK_BOOL) {
			gen_cond(e, x->a, jump_if, l);
			return;
		}
		if (e->E->opt3 && (x->aux == CK_INT || x->aux == CK_NOP || x->aux == CK_P2I) &&
		    (is_integer(c, x->ty) || is_ptr(c, x->ty)) && (is_integer(c, N(x->a)->ty) || is_ptr(c, N(x->a)->ty)) &&
		    !is_wide(c, x->ty) && !is_wide(c, N(x->a)->ty) && tsize(c, x->ty) >= tsize(c, N(x->a)->ty)) {
			/* a widening conversion keeps the truth */
			gen_cond(e, x->a, jump_if, l);
			return;
		}
		break;
	case N_BUILTIN:
		if (e->E->opt3 && (x->aux == BI_EXPECT || x->aux == BI_EXPECT_PROB) && x->c) {
			const uint32_t *arg = &c->lists[x->b];
			bool pure = true;

			for (uint32_t i = 1; i < x->c; i++)
				pure &= !side_effects(c, arg[i]);
			if (pure) {
				gen_cond(e, arg[0], jump_if, l);
				return;
			}
		}
		break;
	case N_BINARY:
		if (em_is_cmp(x->aux) && !is_wide(c, N(x->a)->ty) && !is_flt(c, N(x->a)->ty)) {
			val v = gen(e, n);

			if (v.k == V_CONST || v.k == V_ADDR) {
				test_zero(e, &v, x->ty, l, jump_if);
				return;
			}
			/* gen left a cset: turn it back into a branch on the flags */
			if (e->reach && e->nin && e->in[e->nin - 1].op == I_CSINC && e->in[e->nin - 1].rn == ZR &&
			    e->in[e->nin - 1].rm == ZR && e->in[e->nin - 1].rd == v.r) {
				int cond = e->in[e->nin - 1].cond ^ 1;

				e->nin--;
				bcond_opt(e, jump_if ? cond : cond ^ 1, l);
				val_free(e, &v);
				return;
			}
			test_zero(e, &v, x->ty, l, jump_if);
			return;
		}
		break;
	}
	{
		val v = gen(e, n);

		test_zero(e, &v, x->ty, l, jump_if);
	}
}
