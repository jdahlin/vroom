/* vcc front end: _Complex arithmetic */
#include "ccimpl.h"

VPRIV qty cplx_elem(cc *c, qty t) { return c->tags[T(c, t)->aux].f[0].ty; }

VPRIV qty ty_complex(cc *c, qty et)
{
	int k;

	et = QUNQ(canon(c, et));
	if (KIND(c, et) == TY_ENUM)
		et = QUNQ(T(c, et)->base);
	k = KIND(c, et);
	if (!c->cplx[k]) {
		uint32_t ti = tag_new(c, TY_STRUCT, 0, 0);
		tag *r = &c->tags[ti];

		r->nfield = 2;
		r->f = halloc(c, 2 * sizeof(field));
		memset(r->f, 0, 2 * sizeof(field));
		r->f[0].ty = r->f[1].ty = prim(k);
		r->f[1].off = 8 * ty_size(c, prim(k));
		r->flags |= RF_COMPLETE | RF_COMPLEX;
		lay_record(c, r, NULL, NULL);
		c->cplx[k] = r->ty;
	}
	return QT(c->cplx[k], 0);
}

VPRIV uint32_t mk_cplx(cc *c, qty t, uint32_t re, uint32_t im, uint32_t loc)
{
	return mk2(c, N_CPLX, QUNQ(t), loc, re, im);
}

VPRIV uint32_t zero_of(cc *c, qty t, uint32_t loc)
{
	return is_float(c, t) ? mk_flt(c, 0, QUNQ(t), loc) : mk_int(c, 0, QUNQ(t), loc);
}

/* evaluating n twice is the same as once (and cheap) */
static bool cplx_pure(cc *c, uint32_t n, int d)
{
	const node *x = N(n);

	if (d > 8)
		return false;
	switch (x->op) {
	case N_NUM: case N_FNUM:
		return true;
	case N_VAR:
		return !(QQ(x->ty) & Q_VOLATILE);
	case N_MEMBER: case N_CAST: case N_UNARY: case N_DEREF:
		return !(QQ(x->ty) & Q_VOLATILE) && cplx_pure(c, x->a, d + 1);
	case N_BINARY: case N_INDEX: case N_CPLX:
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

/* part k (0 real, 1 imaginary) of complex n */
VPRIV uint32_t cplx_part(cc *c, uint32_t n, int k)
{
	qty t = N(n)->ty;
	uint32_t m;

	if (N(n)->op == N_CPLX)
		return k ? N(n)->b : N(n)->a;
	m = mk(c, N_MEMBER, qualify(c, cplx_elem(c, t), QQ(t)), N(n)->loc);
	N(m)->a = n;
	N(m)->b = T(c, t)->aux;
	N(m)->c = (uint32_t)k;
	N(m)->flags = N(n)->flags & NF_LVALUE;
	return m;
}

/* both parts of complex n, each usable more than once */
static void cplx_parts(cc *c, uint32_t n, uint32_t *re, uint32_t *im, uint32_t *pre)
{
	if (N(n)->op == N_CPLX && !cplx_pure(c, n, 0)) {
		uint32_t p1, p2;

		*re = cplx_reuse(c, N(n)->a, &p1);
		*im = cplx_reuse(c, N(n)->b, &p2);
		*pre = p1 && p2 ? mk2(c, N_COMMA, N(*im)->ty, N(n)->loc, p1, p2) : p1 ? p1 : p2;
		return;
	}
	n = cplx_reuse(c, n, pre);
	*re = cplx_part(c, n, 0);
	*im = cplx_part(c, n, 1);
}

/* an arithmetic or complex rvalue as complex type t */
VPRIV uint32_t to_complex(cc *c, uint32_t n, qty t)
{
	qty et = cplx_elem(c, t);
	uint32_t loc = N(n)->loc;

	if (is_complex(c, N(n)->ty)) {
		uint32_t re, im, pre = 0;

		if (QI(canon(c, N(n)->ty)) == QI(canon(c, t)))
			return n;
		if (N(n)->op == N_CPLX) {
			re = N(n)->a, im = N(n)->b;
		} else {
			n = cplx_reuse(c, n, &pre);
			re = cplx_part(c, n, 0), im = cplx_part(c, n, 1);
		}
		return with_pre(c, pre, mk_cplx(c, t, convert(c, rvalue(c, re), et), convert(c, rvalue(c, im), et), loc));
	}
	return mk_cplx(c, t, convert(c, n, et), zero_of(c, et, loc), loc);
}

/* complex n != 0 */
VPRIV uint32_t cplx_bool(cc *c, uint32_t n)
{
	uint32_t re, im, pre, loc = N(n)->loc;
	qty et = cplx_elem(c, N(n)->ty);

	cplx_parts(c, n, &re, &im, &pre);
	re = mk_bin(c, T_EXCLAIMEQ, prim(TY_INT), re, zero_of(c, et, loc), loc);
	im = mk_bin(c, T_EXCLAIMEQ, prim(TY_INT), im, zero_of(c, et, loc), loc);
	return with_pre(c, pre, mk2(c, N_LOGOR, prim(TY_INT), loc, re, im));
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

	N(r)->aux = T_MINUS;
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

/* l op r with a complex operand: + - * / == != on the parts */
VPRIV uint32_t cplx_binop(cc *c, int op, uint32_t l, uint32_t r, uint32_t loc)
{
	qty lt = N(l)->ty, rt = N(r)->ty;
	bool lc = is_complex(c, lt), rc = is_complex(c, rt);
	qty le = lc ? cplx_elem(c, lt) : lt, re_ = rc ? cplx_elem(c, rt) : rt, et, ct;
	uint32_t a, b = 0, cr, d = 0, p1 = 0, p2 = 0, x, y;

	if (!is_arith(c, le) || !is_arith(c, re_) ||
	    !(op == T_PLUS || op == T_MINUS || op == T_STAR || op == T_SLASH || op == T_EQUALEQUAL || op == T_EXCLAIMEQ)) {
		bad_operands(c, op, l, r, loc);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	et = arith_conv(c, promote(c, QUNQ(le)), promote(c, QUNQ(re_)));
	ct = ty_complex(c, et);
	if (lc)
		cplx_parts(c, to_complex(c, l, ct), &a, &b, &p1);
	else
		a = cplx_reuse(c, convert(c, l, et), &p1);
	if (rc)
		cplx_parts(c, to_complex(c, r, ct), &cr, &d, &p2);
	else
		cr = cplx_reuse(c, convert(c, r, et), &p2);
#define B(o, u, v) mk_bin(c, o, et, u, v, loc)
	switch (op) {
	case T_PLUS: case T_MINUS:
		x = B(op, a, cr);
		y = !lc ? (op == T_MINUS ? neg(c, d) : d) : !rc ? b : B(op, b, d);
		break;
	case T_STAR:
		if (!lc)
			x = B(T_STAR, a, cr), y = B(T_STAR, a, d);
		else if (!rc)
			x = B(T_STAR, a, cr), y = B(T_STAR, b, cr);
		else
			x = B(T_MINUS, B(T_STAR, a, cr), B(T_STAR, b, d)), y = B(T_PLUS, B(T_STAR, a, d), B(T_STAR, b, cr));
		break;
	case T_SLASH:
		if (!rc) {
			x = B(T_SLASH, a, cr), y = B(T_SLASH, b, cr);
		} else {
			uint32_t den = B(T_PLUS, B(T_STAR, cr, cr), B(T_STAR, d, d));

			if (!lc) {
				x = B(T_SLASH, B(T_STAR, a, cr), den);
				y = B(T_SLASH, neg(c, B(T_STAR, a, d)), den);
			} else {
				x = B(T_SLASH, B(T_PLUS, B(T_STAR, a, cr), B(T_STAR, b, d)), den);
				y = B(T_SLASH, B(T_MINUS, B(T_STAR, b, cr), B(T_STAR, a, d)), den);
			}
		}
		break;
	default: {	/* == != */
		int lop = op == T_EQUALEQUAL ? N_LOGAND : N_LOGOR;
		uint32_t z = zero_of(c, et, loc);

		x = mk_bin(c, op, prim(TY_INT), a, cr, loc);
		y = mk_bin(c, op, prim(TY_INT), lc ? b : z, rc ? d : z, loc);
		return with_pre(c, p1, with_pre(c, p2, mk2(c, lop, prim(TY_INT), loc, x, y)));
	}
	}
#undef B
	return with_pre(c, p1, with_pre(c, p2, mk_cplx(c, ct, x, y, loc)));
}

/* -x, ~x (conjugate), +x of a complex x */
VPRIV uint32_t cplx_unary(cc *c, int op, uint32_t n, uint32_t loc)
{
	uint32_t re, im, pre;
	qty t = QUNQ(N(n)->ty);

	if (op == T_PLUS)
		return n;
	cplx_parts(c, n, &re, &im, &pre);
	return with_pre(c, pre, mk_cplx(c, t, op == T_MINUS ? neg(c, re) : re, neg(c, im), loc));
}
