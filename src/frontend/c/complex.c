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

/* Scale without first constructing a power outside binary128's range. */
static u128 cplx_scale(u128 q, int shift)
{
	int sign, exp;
	u128 mant;

	if (!q_nz(q) || q_isinf(q) || q_isnan(q))
		return q;
	q_un(q, &sign, &exp, &mant);
	return q_rndp(sign, exp + shift, mant << 3, 112);
}

static u128 cplx_round(cc *c, qty t, u128 q)
{
	if (is_q(c, t))
		return q_to_p(q, q_prec(c, t));
	if (KIND(c, t) == TY_FLOAT16)
		return q_of_d(h_to_d(q_to_h(q)));
	return q_of_d(KIND(c, t) == TY_FLOAT ? (double)q_to_f(q) : q_to_d(q));
}

/* Clang's Annex G constant division: scale the denominator, rounding every
 * operation at the target precision, including static initializers. */
static uint32_t cplx_divconst(cc *c, qty ct, qty et, uint32_t *args, uint32_t loc)
{
	u128 q[4], den, re, im;
	int be = 0;

	for (int i = 0; i < 4; i++) {
		cval v;

		if (!cc_eval(c, args[i], &v) || v.kind != CV_FLT)
			return 0;
		q[i] = v.ld ? v.q : q_of_d(v.f);
	}
	u128 max = q[2] & ~Q_SIGN, other = q[3] & ~Q_SIGN;
	if (q_isnan(max) || (!q_isnan(other) && other > max))
		max = other;
	if (q_nz(max) && !q_isnan(max) && !q_isinf(max)) {
		int sign, exp;
		u128 mant;

		q_un(max, &sign, &exp, &mant);
		q_norm(&exp, &mant);
		be = exp - 16383;
	}
#define CR(x) cplx_round(c, et, (x))
#define CM(a, b) CR(q_mul((a), (b)))
#define CA(a, b) CR(q_add((a), (b)))
#define CS(a, b) CR(q_sub((a), (b)))
#define CD(a, b) CR(q_div((a), (b)))
	q[2] = CR(cplx_scale(q[2], -be));
	q[3] = CR(cplx_scale(q[3], -be));
	den = CA(CM(q[2], q[2]), CM(q[3], q[3]));
	re = CR(cplx_scale(CD(CA(CM(q[0], q[2]), CM(q[1], q[3])), den), -be));
	im = CR(cplx_scale(CD(CS(CM(q[1], q[2]), CM(q[0], q[3])), den), -be));
	/* Annex G recovery, matching the helpers for zero and infinite operands. */
	if (q_isnan(re) && q_isnan(im)) {
		u128 inf = (u128)0x7fff << 112, one = (u128)16383 << 112;
		if (!q_nz(den) && (!q_isnan(q[0]) || !q_isnan(q[1]))) {
			inf |= q[2] & Q_SIGN;
			re = CM(inf, q[0]);
			im = CM(inf, q[1]);
		} else if ((q_isinf(q[0]) || q_isinf(q[1])) &&
		           !q_isinf(q[2]) && !q_isnan(q[2]) && !q_isinf(q[3]) && !q_isnan(q[3])) {
			q[0] = (q[0] & Q_SIGN) | (q_isinf(q[0]) ? one : 0);
			q[1] = (q[1] & Q_SIGN) | (q_isinf(q[1]) ? one : 0);
			re = CM(inf, CA(CM(q[0], q[2]), CM(q[1], q[3])));
			im = CM(inf, CS(CM(q[1], q[2]), CM(q[0], q[3])));
		} else if ((q_isinf(q[2]) || q_isinf(q[3])) &&
		           !q_isinf(q[0]) && !q_isnan(q[0]) && !q_isinf(q[1]) && !q_isnan(q[1])) {
			q[2] = (q[2] & Q_SIGN) | (q_isinf(q[2]) ? one : 0);
			q[3] = (q[3] & Q_SIGN) | (q_isinf(q[3]) ? one : 0);
			re = CM(0, CA(CM(q[0], q[2]), CM(q[1], q[3])));
			im = CM(0, CS(CM(q[1], q[2]), CM(q[0], q[3])));
		}
	}
#undef CR
#undef CM
#undef CA
#undef CS
#undef CD
	if (is_q(c, et))
		return mk_cplx(c, ct, mk_fltq(c, q_to_p(re, q_prec(c, et)), et, loc),
		               mk_fltq(c, q_to_p(im, q_prec(c, et)), et, loc), loc);
	if (KIND(c, et) == TY_FLOAT16)
		return mk_cplx(c, ct, mk_flt(c, h_to_d(q_to_h(re)), et, loc),
		               mk_flt(c, h_to_d(q_to_h(im)), et, loc), loc);
	return mk_cplx(c, ct, mk_flt(c, KIND(c, et) == TY_FLOAT ? q_to_f(re) : q_to_d(re), et, loc),
	               mk_flt(c, KIND(c, et) == TY_FLOAT ? q_to_f(im) : q_to_d(im), et, loc), loc);
}

/* The runtime helper owns overflow/underflow scaling and IEEE special cases.
 * It returns the real complex ABI type, rather than an ordinary two-field struct. */
static uint32_t cplx_divcall(cc *c, qty ct, qty et, uint32_t *a, uint32_t loc)
{
	qty result_ty = ct;
	if (KIND(c, et) == TY_FLOAT16) {
		et = prim(TY_FLOAT);
		ct = ty_complex(c, et);
	}
	const char *name = KIND(c, et) == TY_FLOAT ? "__divsc3" : !is_q(c, et) ? "__divdc3" :
	                   q_prec(c, et) == Q_P80 ? "__divxc3" : "__divtc3";
	uint32_t id = vintern_cstr(name), s, fn, n;
	qty params[4] = { et, et, et, et };
	qty ft = ty_func(c, ct, params, 4, 0);
	vec args;

	s = sym_new(c, S_FUNC, id, ft, loc);
	c->syms[s].sc = SC_EXTERN;
	c->syms[s].flags |= SF_GLOBAL | SF_USED;
	fn = mk(c, N_VAR, ft, loc);
	N(fn)->a = s;
	vinit(&args);
	for (int i = 0; i < 4; i++)
		vpush(c, &args, convert(c, rvalue(c, a[i]), et));
	n = mk(c, N_CALL, ct, loc);
	N(n)->a = decay(c, fn);
	N(n)->b = vfreeze(c, &args);
	N(n)->c = 4;
	return to_complex(c, n, result_ty);
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
			if (is_float(c, et)) {
				uint32_t args[4] = { a, lc ? b : zero_of(c, et, loc), cr, d };
				uint32_t result = cplx_divconst(c, ct, et, args, loc);

				if (!result)
					result = cplx_divcall(c, ct, et, args, loc);
				return with_pre(c, p1, with_pre(c, p2, result));
			}
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

/* Atomically update both components with a compare-exchange retry loop.
 * The expected value is refreshed by each failed CAS, including the imaginary
 * payload; only the successful old/new snapshot becomes the expression value. */
static uint32_t cplx_atomic_incdec(cc *c, uint32_t n, int op, uint32_t loc)
{
	qty t = QUNQ(ty_unatomic(c, N(n)->ty)), et = cplx_elem(c, t);
	bool inc = op == N_PREINC || op == N_POSTINC, post = op == N_POSTINC || op == N_POSTDEC;
	uint32_t vars[2], pointers[2], pre;
	uint32_t address = mk(c, N_ADDR, ty_ptr(c, N(n)->ty), loc);
	N(address)->a = n;
	address = cplx_reuse(c, address, &pre);
	for (int i = 0; i < 2; i++) {
		uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
		c->syms[sym].sc = SC_AUTO;
		vars[i] = mk(c, N_VAR, t, loc);
		N(vars[i])->a = sym;
		N(vars[i])->flags = NF_LVALUE;
		pointers[i] = mk(c, N_ADDR, ty_ptr(c, t), loc);
		N(pointers[i])->a = vars[i];
	}
	vec args, body;
	vinit(&args);
	vinit(&body);
	if (pre) {
		vpush(c, &body, mk2(c, N_DECL, prim(TY_VOID), loc, N(N(pre)->a)->a, 0));
		vpush(c, &body, mkstmt(c, N_EXPR, loc, pre, 0, 0));
	}
	for (int i = 0; i < 2; i++)
		vpush(c, &body, mk2(c, N_DECL, prim(TY_VOID), loc, N(vars[i])->a, 0));
	vpush(c, &args, address);
	vpush(c, &args, pointers[0]);
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	uint32_t load = mk_builtin(c, BI_ATOMIC_LOAD, prim(TY_VOID), loc, &args);
	vpush(c, &body, mkstmt(c, N_EXPR, loc, load, 0, 0));
	uint32_t re = binop(c, inc ? T_PLUS : T_MINUS, rvalue(c, cplx_part(c, vars[0], 0)), mk_int(c, 1, prim(TY_INT), loc), loc);
	uint32_t desired = mk_cplx(c, t, convert(c, re, et), rvalue(c, cplx_part(c, vars[0], 1)), loc);
	uint32_t update = assign(c, 0, vars[1], desired, loc);
	args.n = 0;
	vpush(c, &args, address);
	vpush(c, &args, pointers[0]);
	vpush(c, &args, pointers[1]);
	vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	uint32_t cas = mk_builtin(c, BI_ATOMIC_CMPXCHG, prim(TY_BOOL), loc, &args);
	uint32_t retry = mk_bin(c, T_EQUALEQUAL, prim(TY_INT), cas, mk_int(c, 0, prim(TY_INT), loc), loc);
	vpush(c, &body, mkstmt(c, N_DO, loc, retry, mkstmt(c, N_EXPR, loc, update, 0, 0), 0));
	vpush(c, &body, mkstmt(c, N_EXPR, loc, rvalue(c, vars[post ? 0 : 1]), 0, 0));
	uint32_t block = mkstmt(c, N_BLOCK, loc, 0, vfreeze(c, &body), body.n);
	uint32_t out = mk(c, N_STMTEXPR, t, loc);
	N(out)->a = block;
	N(out)->aux = (uint16_t)op;
	return out;
}

/* Increment/decrement only the real component, retaining the original
 * imaginary representation and evaluating the operand's location once. */
VPRIV uint32_t cplx_incdec(cc *c, uint32_t n, int op, uint32_t loc)
{
	bool inc = op == N_PREINC || op == N_POSTINC;
	bool post = op == N_POSTINC || op == N_POSTDEC;
	qty t = QUNQ(ty_unatomic(c, N(n)->ty)), et = cplx_elem(c, t);
	uint32_t pre = 0, save, tmp, value, put;
	if (!c->o->c2y) {
		static const char *const g[] = { "pedantic", NULL };
		EXT_WARN_MS(c, inc ? "++" : "--", "c2y-extensions", g, loc,
		            "'%s' on an object of complex type is a C2y extension", inc ? "++" : "--");
	}
	if (T(c, N(n)->ty)->flags & TF_ATOMIC) {
		if (atomic_wide(c, N(n)->ty))
			return atomic_assign_expr(c, n, 0, inc ? T_PLUS : T_MINUS, post, loc);
		if (ty_size(c, t) <= 8)
			return cplx_atomic_incdec(c, n, op, loc);
		cc_error(c, loc, "atomic complex increment and decrement are not supported yet");
		return err_node(c, loc);
	}
	if (N(n)->op != N_VAR) {
		/* This is an internal address, also valid for a register aggregate. */
		uint32_t address = mk(c, N_ADDR, ty_ptr(c, N(n)->ty), loc);
		N(address)->a = n;
		address = cplx_reuse(c, address, &pre);
		n = deref(c, address, loc);
	}
	uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	tmp = mk(c, N_VAR, t, loc);
	N(tmp)->a = sym;
	N(tmp)->flags = NF_LVALUE;
	save = mk2(c, N_ASSIGN, t, loc, tmp, rvalue(c, n));
	value = binop(c, inc ? T_PLUS : T_MINUS, rvalue(c, cplx_part(c, tmp, 0)), mk_int(c, 1, prim(TY_INT), loc), loc);
	value = convert(c, value, et);
	/* Materialize the arithmetic once: storing the updated real component
	 * and producing a prefix result must not repeat a trapping FP operation. */
	uint32_t desired_sym = sym_new(c, S_VAR, 0, et, loc);
	c->syms[desired_sym].sc = SC_AUTO;
	uint32_t desired = mk(c, N_VAR, et, loc);
	N(desired)->a = desired_sym;
	N(desired)->flags = NF_LVALUE;
	uint32_t compute = mk2(c, N_ASSIGN, et, loc, desired, value);
	value = rvalue(c, desired);
	put = assign(c, 0, cplx_part(c, n, 0), value, loc);
	uint32_t result = post ? rvalue(c, tmp) : mk_cplx(c, t, value, rvalue(c, cplx_part(c, tmp, 1)), loc);
	uint32_t out = with_pre(c, pre, with_pre(c, save, with_pre(c, compute, with_pre(c, put, result))));
	N(out)->aux = (uint16_t)op; /* retain the source operation for unused-value diagnostics */
	return out;
}
