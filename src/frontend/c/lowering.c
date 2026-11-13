/* vcc front end: helpers that lowering of complex, wide atomics and _BitInt casts build AST with */
#include "ccimpl.h"

VPRIV uint32_t zero_of(cc *c, qty t, uint32_t loc)
{
	return is_float(c, t) ? mk_flt(c, 0, QUNQ(t), loc) : mk_int(c, 0, QUNQ(t), loc);
}

/* evaluating n twice is the same as once (and cheap) */
VPRIV bool cplx_pure(cc *c, uint32_t n, int d)
{
	const node *x = N(n);

	if (d > 8)
		return false;
	switch (x->op) {
	case N_NUM: case N_FNUM:
		return true;
	case N_BUILTIN: {
		cval v;

		return cc_eval(c, n, &v) && (v.kind == CV_INT || v.kind == CV_FLT);
	}
	case N_VAR:
		return !(QQ(x->ty) & Q_VOLATILE);
	case N_MEMBER: case N_CAST: case N_UNARY: case N_DEREF:
		return !(QQ(x->ty) & Q_VOLATILE) && cplx_pure(c, x->a, d + 1);
	case N_BINARY: case N_INDEX: case N_CPLX: case N_LOGAND: case N_LOGOR: case N_COMMA:
		return cplx_pure(c, x->a, d + 1) && cplx_pure(c, x->b, d + 1);
	default:
		return false;
	}
}

/* n, or a temporary holding it (*pre: the assignment to run first) */
VPRIV uint32_t cplx_reuse(cc *c, uint32_t n, uint32_t *pre)
{
	qty t = QUNQ(N(n)->ty);
	uint32_t s, v;

	*pre = 0;
	/* A constant conditional must not acquire an automatic temporary merely
	   because its selected complex value will be used twice. */
	if (N(n)->op == N_COND && cplx_pure(c, N(n)->a, 0)) {
		cval cv;
		if (cc_eval(c, N(n)->a, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT)) {
			bool yes = cv.kind == CV_INT ? cv.i != 0 : cv.ld ? vsoftfloat_is_nonzero(cv.q) : cv.f != 0;
			return cplx_reuse(c, yes ? (N(n)->b ? N(n)->b : N(n)->a) : N(n)->c, pre);
		}
	}
	if (cplx_pure(c, n, 0))
		return n;
	s = sym_new(c, S_VAR, 0, t, N(n)->loc);
	c->syms[s].sc = SC_AUTO;
	v = mk(c, N_VAR, t, N(n)->loc);
	N(v)->a = s;
	N(v)->flags = NF_LVALUE;
	*pre = mk2(c, N_ASSIGN, t, N(n)->loc, v, n);
	return v;
}

VPRIV uint32_t with_pre(cc *c, uint32_t pre, uint32_t n)
{
	return pre ? mk2(c, N_COMMA, N(n)->ty, N(n)->loc, pre, n) : n;
}

/* a condition: complex values test both parts */
VPRIV uint32_t scalar_cond(cc *c, uint32_t n, const char *what)
{
	if (is_complex(c, N(n)->ty))
		return cplx_bool(c, n);
	need_scalar(c, n, what);
	return n;
}

VPRIV uint32_t neg(cc *c, uint32_t n)
{
	uint32_t r = mk(c, N_UNARY, N(n)->ty, N(n)->loc);

	N(r)->aux = LEX_TOKEN_MINUS;
	N(r)->a = n;
	return r;
}

/* "invalid operands to binary expression ('L' and 'R')", ranges on both */
VPRIV void bad_operands(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	pp_span s[2] = { rng(c, l), rng(c, r) };

	if ((N(l)->flags | N(r)->flags) & NF_ERR)
		return;
	c->dms = punct_spell(op);
	report(c, "error", loc, s, 2, NULL, 0, "invalid operands to binary expression ('%s' and '%s')",
	       tyname(c, N(l)->ty), tyname(c, N(r)->ty));
}
