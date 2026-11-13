/* vcc code generator: floating point (long double through libcalls) */
#include "../../../emit/emitimpl.h"
#include "../../../lib/f128.h"

enum { FV0 = 16, FV1 = 17 };

VPRIV int fpk(ecx *e, qty t)
{
	uint64_t sz = tsize(e->c, t);

	if (sz != 2 && sz != 4 && sz != 8)
		efatal(e, e->curloc, "%s floating point is not supported", sz == 2 ? "16-bit" : "128-bit");
	return sz == 8;
}

static double f_val(int d, i128 bits)
{
	if (d) {
		uint64_t u = (uint64_t)bits;
		double x;

		memcpy(&x, &u, 8);
		return x;
	} else {
		uint32_t u = (uint32_t)bits;
		float x;

		memcpy(&x, &u, 4);
		return x;
	}
}

VPRIV val f_const(int d, double x)
{
	if (d) {
		uint64_t u;

		memcpy(&u, &x, 8);
		return vconst((i128)u);
	} else {
		float f = (float)x;
		uint32_t u;

		memcpy(&u, &f, 4);
		return vconst((i128)u);
	}
}

/* v into FP register vr */
static void f_load(ecx *e, int d, int vr, val *v)
{
	int r = toreg(e, v);

	word(e, f_in(d, vr, r));
}

/* an FP value of sz bytes (4, 8, 16) between v<vr> and memory at base + off, through x16 */
VPRIV void vr_store(ecx *e, int sz, int vr, int base, int64_t off)
{
	word(e, f_out(sz >= 8, R_X16, vr));
	mem(e, I_ST, sz >= 8 ? 3 : sz == 2 ? 1 : 2, 0, R_X16, base, off);
	if (sz == 16) {
		word(e, q_out_hi(R_X16, vr));
		mem(e, I_ST, 3, 0, R_X16, base, off + 8);
	}
}
VPRIV void vr_load(ecx *e, int sz, int vr, int base, int64_t off)
{
	mem(e, I_LD, sz >= 8 ? 3 : sz == 2 ? 1 : 2, 0, R_X16, base, off);
	word(e, f_in(sz >= 8, vr, R_X16));
	if (sz == 16) {
		mem(e, I_LD, 3, 0, R_X16, base, off + 8);
		word(e, q_in_hi(vr, R_X16));
	}
}

/* host side: binary128 bits of a double (exact), and back (nearest) */
VPRIV i128 ld_of_d(double x)
{
	uint64_t u;
	int s, ex;
	u128 m;

	memcpy(&u, &x, 8);
	s = (int)(u >> 63), ex = (int)(u >> 52) & 0x7ff, m = u & ((1ull << 52) - 1);
	if (ex == 0x7ff)
		return (i128)((u128)s << 127 | (u128)0x7fff << 112 | m << 60 | (m ? (u128)1 << 111 : 0));
	if (!ex) {
		if (!m)
			return (i128)((u128)s << 127);
		ex = 1;
		while (!(m >> 52))
			m <<= 1, ex--;
		m &= (1ull << 52) - 1;
	}
	return (i128)((u128)s << 127 | (u128)(ex - 1023 + 16383) << 112 | m << 60);
}
static double ld_to_d(i128 b)
{
	u128 u = (u128)b, m = u & (((u128)1 << 112) - 1);
	int ex = (int)(u >> 112) & 0x7fff;
	double x;

	if (ex == 0x7fff)
		x = m ? __builtin_nan("") : __builtin_inf();
	else
		x = __builtin_ldexp((double)(m | (ex ? (u128)1 << 112 : 0)), (ex ? ex : 1) - 16383 - 112);
	return u >> 127 ? -x : x;
}

/* a floating constant of type t */
VPRIV val f_const_t(ecx *e, qty t, double x)
{
	if (KIND(e->c, t) == TY_BFLOAT16)
		return vconst(q_to_bf(q_of_d(x)));
	if (KIND(e->c, t) == TY_FLOAT16)
		return vconst(q_to_h(q_of_d(x)));
	if (is_ld(e->c, t))
		return vconst(ld_of_d(x));
	return f_const(fpk(e, t), x);
}

/* a folded floating constant of type t: binary128 values keep their bits */
VPRIV val f_const_cv(ecx *e, qty t, const cval *cv)
{
	if (is_ld(e->c, t))
		return vconst(cv->ld ? (i128)cv->q : ld_of_d(cv->f));
	if (cv->ld && q_isnan(cv->q)) {
		int sz = (int)tsize(e->c, t);
		return vconst(q_nan_storage(cv->q, KIND(e->c, t) == TY_BFLOAT16 ? 7 : sz == 2 ? 10 : sz == 4 ? 23 : 52, KIND(e->c, t) == TY_BFLOAT16 ? 8 : sz == 2 ? 5 : sz == 4 ? 8 : 11));
	}
	return f_const_t(e, t, cv->f);
}

static val ld_call(ecx *e, int fn, val *v, qty *t, int n, qty rty)
{
	return call_named(e, FIXED(fn), v, t, n, rty, false);
}

VPRIV val ld_compare(ecx *e, int op, qty t, val a, val b)
{
	val v[2] = { a, b };
	qty ts[2] = { t, t };
	int fn, cond, r;
	val res;

	switch (op) {
	case T_EQUALEQUAL: fn = FN_EQTF2, cond = C_EQ; break;
	case T_EXCLAIMEQ: fn = FN_NETF2, cond = C_NE; break;
	case T_LESS: fn = FN_LTTF2, cond = C_LT; break;
	case T_LESSEQ: fn = FN_LETF2, cond = C_LE; break;
	case T_GREATER: fn = FN_GTTF2, cond = C_GT; break;
	case T_GREATEREQ: fn = FN_GETF2, cond = C_GE; break;
	default: efatal(e, e->curloc, "unsupported floating point comparison");
	}
	res = ld_call(e, fn, v, ts, 2, prim(TY_INT));
	r = toreg(e, &res);
	opi(e, I_SUBSI, 0, ZR, r, 0);
	cset(e, r, cond);
	return vreg(r);
}

VPRIV val f_arith(ecx *e, int op, qty ty, val a, val b)
{
	int d, ra;
	uint32_t w;

	if (KIND(e->c, ty) == TY_FLOAT16 || KIND(e->c, ty) == TY_BFLOAT16) {
		a = f_cast(e, a, ty, prim(TY_FLOAT));
		b = f_cast(e, b, ty, prim(TY_FLOAT));
		return f_cast(e, f_arith(e, op, prim(TY_FLOAT), a, b), prim(TY_FLOAT), ty);
	}
	if (is_ld(e->c, ty)) {
		val v[2] = { a, b };
		qty ts[2] = { ty, ty };
		int fn = op == T_PLUS ? FN_ADDTF3 : op == T_MINUS ? FN_SUBTF3 : op == T_STAR ? FN_MULTF3 : FN_DIVTF3;

		if (op != T_PLUS && op != T_MINUS && op != T_STAR && op != T_SLASH)
			efatal(e, e->curloc, "unsupported floating point operator");
		return ld_call(e, fn, v, ts, 2, ty);
	}
	d = fpk(e, ty);
	if (a.k == V_CONST && b.k == V_CONST) {
		double x = f_val(d, a.i), y = f_val(d, b.i);

		switch (op) {
		case T_PLUS: return f_const(d, d ? x + y : (double)((float)x + (float)y));
		case T_MINUS: return f_const(d, d ? x - y : (double)((float)x - (float)y));
		case T_STAR: return f_const(d, d ? x * y : (double)((float)x * (float)y));
		case T_SLASH: return f_const(d, d ? x / y : (double)((float)x / (float)y));
		}
	}
	switch (op) {
	case T_PLUS: w = 0x1e202800u; break;
	case T_MINUS: w = 0x1e203800u; break;
	case T_STAR: w = 0x1e200800u; break;
	case T_SLASH: w = 0x1e201800u; break;
	default: efatal(e, e->curloc, "unsupported floating point operator");
	}
	f_load(e, d, FV0, &a);
	f_load(e, d, FV1, &b);
	val_free(e, &b);
	ra = a.r;
	word(e, w | (unsigned)d << 22 | FV1 << 16 | FV0 << 5 | FV0);
	word(e, f_out(d, ra, FV0));
	return a;
}

/* fcmp a, b (b none: #0.0); the condition for op */
VPRIV int f_cmp(ecx *e, int op, int d, val *a, val *b)
{
	f_load(e, d, FV0, a);
	if (b) {
		f_load(e, d, FV1, b);
		word(e, 0x1e202000u | (unsigned)d << 22 | FV1 << 16 | FV0 << 5);
	} else {
		word(e, 0x1e202008u | (unsigned)d << 22 | FV0 << 5);
	}
	switch (op) {
	case T_EQUALEQUAL: return C_EQ;
	case T_EXCLAIMEQ: return C_NE;	/* true when unordered */
	case T_LESS: return C_MI;
	case T_LESSEQ: return C_LS;
	case T_GREATER: return C_GT;
	case T_GREATEREQ: return C_GE;
	}
	efatal(e, e->curloc, "unsupported floating point comparison");
}

VPRIV val f_compare(ecx *e, int op, qty t, val a, val b)
{
	int d, cond;

	if (KIND(e->c, t) == TY_FLOAT16 || KIND(e->c, t) == TY_BFLOAT16) {
		a = f_cast(e, a, t, prim(TY_FLOAT));
		b = f_cast(e, b, t, prim(TY_FLOAT));
		return f_compare(e, op, prim(TY_FLOAT), a, b);
	}
	if (is_ld(e->c, t))
		return ld_compare(e, op, t, a, b);
	d = fpk(e, t);

	if (a.k == V_CONST && b.k == V_CONST) {
		double x = f_val(d, a.i), y = f_val(d, b.i);

		switch (op) {
		case T_EQUALEQUAL: return vconst(x == y);
		case T_EXCLAIMEQ: return vconst(x != y);
		case T_LESS: return vconst(x < y);
		case T_LESSEQ: return vconst(x <= y);
		case T_GREATER: return vconst(x > y);
		case T_GREATEREQ: return vconst(x >= y);
		}
	}
	cond = f_cmp(e, op, d, &a, &b);
	val_free(e, &b);
	cset(e, a.r, cond);
	return vreg(a.r);
}

/* x != 0.0 as 0/1 (NaN is true, -0.0 false) */
VPRIV val f_tobool(ecx *e, val v, qty t)
{
	int d;

	if (KIND(e->c, t) == TY_FLOAT16 || KIND(e->c, t) == TY_BFLOAT16)
		return f_tobool(e, f_cast(e, v, t, prim(TY_FLOAT)), prim(TY_FLOAT));

	if (is_ld(e->c, t)) {
		if (v.k == V_CONST)
			return vconst(((u128)v.i << 1) != 0);
		return ld_compare(e, T_EXCLAIMEQ, t, v, vconst(0));
	}
	d = fpk(e, t);

	if (v.k == V_CONST)
		return vconst(f_val(d, v.i) != 0);
	f_cmp(e, T_EXCLAIMEQ, d, &v, NULL);
	cset(e, v.r, C_NE);
	return v;
}

VPRIV val f_neg(ecx *e, val v, qty t)
{
	int d;

	if (KIND(e->c, t) == TY_FLOAT16 || KIND(e->c, t) == TY_BFLOAT16) {
		if (v.k == V_CONST)
			return vconst(v.i ^ 0x8000);
		toreg(e, &v);
		opi(e, I_EORI, 0, v.r, v.r, 0x8000);
		return v;
	}

	if (is_ld(e->c, t)) {
		if (v.k == V_CONST)
			return vconst((i128)((u128)v.i ^ (u128)1 << 127));
		toreg2(e, &v, false);
		opi(e, I_EORI, 1, v.r2, v.r2, (int64_t)0x8000000000000000ULL);
		return v;
	}
	d = fpk(e, t);

	if (v.k == V_CONST)
		return f_const(d, -f_val(d, v.i));
	f_load(e, d, FV0, &v);
	word(e, 0x1e214000u | (unsigned)d << 22 | FV0 << 5 | FV0);
	word(e, f_out(d, v.r, FV0));
	return v;
}

/* conversions to and from long double (one side is long double, the other not bool) */
VPRIV val ld_cast(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);

	if (ff && tf) {
		if (is_ld(c, from) && is_ld(c, to))
			return v;
		if (is_ld(c, to)) {
			if (v.k == V_CONST)
				return vconst(ld_of_d(f_val(fpk(e, from), v.i)));
			return ld_call(e, fpk(e, from) ? FN_EXTENDDFTF2 : FN_EXTENDSFTF2, &v, &from, 1, to);
		}
		if (v.k == V_CONST)
			return f_const(fpk(e, to), ld_to_d(v.i));
		return ld_call(e, fpk(e, to) ? FN_TRUNCTFDF2 : FN_TRUNCTFSF2, &v, &from, 1, to);
	}
	if (tf) {
		uint64_t fs = tsize(c, from);
		bool sg = !is_ptr(c, from) && is_signed(c, from);
		qty at;
		int fn;

		if (v.k == V_CONST) {
			i128 x = em_trunc_to(c, v.i, from);

			return vconst(q_of_i(x, sg));
		}
		if (fs == 16) {
			fn = sg ? FN_FLOATTITF : FN_FLOATUNTITF, at = sg ? prim(TY_INT128) : prim(TY_UINT128);
		} else if (fs == 8) {
			fn = sg ? FN_FLOATDITF : FN_FLOATUNDITF, at = sg ? prim(TY_LONG) : prim(TY_ULONG);
		} else {
			toreg(e, &v);
			if (fs < 4)
				extend(e, v.r, v.r, (int)fs, sg);
			fn = sg ? FN_FLOATSITF : FN_FLOATUNSITF, at = sg ? prim(TY_INT) : prim(TY_UINT);
		}
		return ld_call(e, fn, &v, &at, 1, to);
	}
	{
		uint64_t ts = tsize(c, to);
		bool sg = !is_ptr(c, to) && is_signed(c, to);
		int fn;
		qty rt;
		val r;

		if (v.k == V_CONST) {
			double x = ld_to_d(v.i);

			return vconst(em_trunc_to(c, sg ? (i128)x : (i128)(u128)x, to));
		}
		if (ts == 16)
			return ld_call(e, sg ? FN_FIXTFTI : FN_FIXUNSTFTI, &v, &from, 1, to);
		fn = ts == 8 ? (sg ? FN_FIXTFDI : FN_FIXUNSTFDI) : (sg ? FN_FIXTFSI : FN_FIXUNSTFSI);
		rt = ts == 8 ? (sg ? prim(TY_LONG) : prim(TY_ULONG)) : (sg ? prim(TY_INT) : prim(TY_UINT));
		r = ld_call(e, fn, &v, &from, 1, rt);
		if (ts < 4) {
			toreg(e, &r);
			extend(e, r.r, r.r, (int)ts, sg);
		}
		return r;
	}
}

VPRIV val f_cast(ecx *e, val v, qty from, qty to)
{
	cc *c = e->c;
	bool ff = is_flt(c, from), tf = is_flt(c, to);

	if (v.k == V_NONE)
		return v;
	if (KIND(c, from) == TY_BFLOAT16) {
		if (KIND(c, to) == TY_BFLOAT16) return v;
		v = call_named(e, FIXED(FN_EXTENDBFSF2), &v, &from, 1, prim(TY_FLOAT), false);
		return f_cast(e, v, prim(TY_FLOAT), to);
	}
	if (KIND(c, to) == TY_BFLOAT16) {
		if (KIND(c, from) == TY_FLOAT16)
			v = f_cast(e, v, from, prim(TY_FLOAT)), from = prim(TY_FLOAT);
		if (!ff)
			v = f_cast(e, v, from, prim(TY_DOUBLE)), from = prim(TY_DOUBLE);
		return call_named(e, FIXED(is_ld(c, from) ? FN_TRUNCTFBF2 : KIND(c, from) == TY_FLOAT ? FN_TRUNCSFBF2 : FN_TRUNCDFBF2), &v, &from, 1, to, false);
	}
	if (KIND(c, from) == TY_FLOAT16 || KIND(c, to) == TY_FLOAT16) {
		bool fh = KIND(c, from) == TY_FLOAT16;
		qty wide = fh ? to : from;

		if (from == to)
			return v;
		if (fh && (!tf || is_ld(c, to)))
			return f_cast(e, f_cast(e, v, from, prim(TY_FLOAT)), prim(TY_FLOAT), to);
		if (!fh && is_ld(c, from))
			return call_named(e, FIXED(FN_TRUNCTFHF2), &v, &from, 1, to, false);
		if (!fh && !ff)
			return f_cast(e, f_cast(e, v, from, prim(TY_DOUBLE)), prim(TY_DOUBLE), to);
		if (v.k == V_CONST)
			return fh ? f_const_t(e, to, h_to_d((uint16_t)v.i)) : vconst(q_to_h(q_of_d(f_val(fpk(e, from), v.i))));
		{
			bool d = KIND(c, wide) == TY_DOUBLE;
			uint32_t op = fh ? (d ? 0x1ee2c000u : 0x1ee24000u) : (d ? 0x1e63c000u : 0x1e23c000u);

			f_load(e, !fh && d, FV0, &v);
			word(e, op | FV0 << 5 | FV0);
			word(e, f_out(fh && d, v.r, FV0));
			return v;
		}
	}
	if (KIND(c, to) == TY_BOOL)
		return f_tobool(e, v, from);
	if (is_ld(c, from) || is_ld(c, to))
		return ld_cast(e, v, from, to);
	if (ff && tf) {
		int fd = fpk(e, from), td = fpk(e, to);

		if (fd == td)
			return v;
		if (v.k == V_CONST)
			return f_const(td, f_val(fd, v.i));
		f_load(e, fd, FV0, &v);
		word(e, (fd ? 0x1e624000u : 0x1e22c000u) | FV0 << 5 | FV0);	/* fcvt s<-d / d<-s */
		word(e, f_out(td, v.r, FV0));
		return v;
	}
	if (tf) {
		/* integer -> floating */
		int td = fpk(e, to);
		uint64_t fs = tsize(c, from);
		bool sg = !is_ptr(c, from) && is_signed(c, from);

		if (fs == 16) {
			/* __floattidf & co. */
			val r;
			qty ft = sg ? prim(TY_INT128) : prim(TY_UINT128);

			if (v.k == V_CONST)
				return f_const(td, td ? (sg ? (double)v.i : (double)(u128)v.i) :
					       (sg ? (double)(float)v.i : (double)(float)(u128)v.i));
			r = call_named(e, FIXED(sg ? (td ? FN_FLOATTIDF : FN_FLOATTISF) : (td ? FN_FLOATUNTIDF : FN_FLOATUNTISF)),
				       &v, &ft, 1, to, false);
			return r;
		}
		if (v.k == V_CONST) {
			i128 x = em_trunc_to(c, v.i, from);

			/* Round directly to float: going through double loses the
			 * sticky bit for integers just above a float midpoint. */
			return f_const(td, td ? (sg ? (double)(int64_t)x : (double)(uint64_t)x) :
				       (sg ? (double)(float)(int64_t)x : (double)(float)(uint64_t)x));
		}
		toreg(e, &v);
		if (fs < 4)
			extend(e, v.r, v.r, (int)fs, sg);
		/* scvtf/ucvtf v16, x/w */
		word(e, (fs == 8 ? 0x9e220000u : 0x1e220000u) | (sg ? 0 : 0x10000u) | (unsigned)td << 22 | (unsigned)v.r << 5 | FV0);
		word(e, f_out(td, v.r, FV0));
		return v;
	}
	{
		/* floating -> integer (fcvtzs/fcvtzu, x or w) */
		int fd = fpk(e, from);
		uint64_t ts = tsize(c, to);
		bool sg = !is_ptr(c, to) && is_signed(c, to);

		if (ts == 16) {
			/* __fixdfti & co. */
			if (v.k == V_CONST) {
				double x = f_val(fd, v.i);

				return vconst(sg ? (i128)x : (i128)(u128)x);
			}
			return call_named(e, FIXED(sg ? (fd ? FN_FIXDFTI : FN_FIXSFTI) : (fd ? FN_FIXUNSDFTI : FN_FIXUNSSFTI)),
					  &v, &from, 1, to, false);
		}
		if (v.k == V_CONST) {
			double x = f_val(fd, v.i);

			return vconst(em_trunc_to(c, sg ? (i128)(int64_t)x : (i128)(uint64_t)x, to));
		}
		f_load(e, fd, FV0, &v);
		word(e, (ts == 8 ? 0x9e380000u : 0x1e380000u) | (sg ? 0 : 0x10000u) | (unsigned)fd << 22 | FV0 << 5 | (unsigned)v.r);
		if (ts < 4)
			extend(e, v.r, v.r, (int)ts, sg);
		return v;
	}
}
