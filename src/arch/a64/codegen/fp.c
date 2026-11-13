/* vcc code generator: floating point (long double through libcalls) */
#include "../../../codegen/codegen-internal.h"
#include "common/softfloat.h"

enum { FV0 = 16, FV1 = 17 };

VPRIV int fpk(codegen_context *e, ast_qualified_type t)
{
	uint64_t sz = codegen_type_size(e->compile_context, t);

	if (sz != 2 && sz != 4 && sz != 8)
		codegen_fatal_report(e, e->current_location, "%s floating point is not supported", sz == 2 ? "16-bit" : "128-bit");
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

VPRIV codegen_value f_const(int d, double x)
{
	if (d) {
		uint64_t u;

		memcpy(&u, &x, 8);
		return codegen_value_constant((i128)u);
	} else {
		float f = (float)x;
		uint32_t u;

		memcpy(&u, &f, 4);
		return codegen_value_constant((i128)u);
	}
}

/* v into FP register vr */
static void f_load(codegen_context *e, int d, int vr, codegen_value *v)
{
	int r = toreg(e, v);

	word(e, f_in(d, vr, r));
}

/* an FP value of sz bytes (4, 8, 16) between v<vr> and memory at base + off, through x16 */
VPRIV void vr_store(codegen_context *e, int sz, int vr, int base, int64_t off)
{
	word(e, f_out(sz >= 8, R_X16, vr));
	mem(e, I_ST, sz >= 8 ? 3 : sz == 2 ? 1 : 2, 0, R_X16, base, off);
	if (sz == 16) {
		word(e, q_out_hi(R_X16, vr));
		mem(e, I_ST, 3, 0, R_X16, base, off + 8);
	}
}
VPRIV void vr_load(codegen_context *e, int sz, int vr, int base, int64_t off)
{
	mem(e, I_LD, sz >= 8 ? 3 : sz == 2 ? 1 : 2, 0, R_X16, base, off);
	word(e, f_in(sz >= 8, vr, R_X16));
	if (sz == 16) {
		mem(e, I_LD, 3, 0, R_X16, base, off + 8);
		word(e, q_in_hi(vr, R_X16));
	}
}

/* host side: binary128 bits of a double (exact), and back (nearest) */
VPRIV i128 ld_of_d(double x) { return (i128)vsoftfloat_of_double(x); }
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
VPRIV codegen_value f_const_t(codegen_context *e, ast_qualified_type t, double x)
{
	if (KIND(e->compile_context, t) == AST_TYPE_BFLOAT16)
		return codegen_value_constant(vsoftfloat_to_bfloat16(vsoftfloat_of_double(x)));
	if (KIND(e->compile_context, t) == AST_TYPE_FLOAT16)
		return codegen_value_constant(vsoftfloat_to_half(vsoftfloat_of_double(x)));
	if (codegen_type_is_long_double(e->compile_context, t))
		return codegen_value_constant(ld_of_d(x));
	return f_const(fpk(e, t), x);
}

/* a folded floating constant of type t: binary128 values keep their bits */
VPRIV codegen_value f_const_cv(codegen_context *e, ast_qualified_type t, const cc_value *cv)
{
	if (codegen_type_is_long_double(e->compile_context, t))
		return codegen_value_constant(cv->ld ? (i128)cv->q : ld_of_d(cv->f));
	if (cv->ld && vsoftfloat_is_nan(cv->q)) {
		int sz = (int)codegen_type_size(e->compile_context, t);
		return codegen_value_constant(vsoftfloat_nan_storage(cv->q, KIND(e->compile_context, t) == AST_TYPE_BFLOAT16 ? 7 : sz == 2 ? 10 : sz == 4 ? 23 : 52, KIND(e->compile_context, t) == AST_TYPE_BFLOAT16 ? 8 : sz == 2 ? 5 : sz == 4 ? 8 : 11));
	}
	return f_const_t(e, t, cv->f);
}

static codegen_value ld_call(codegen_context *e, int fn, codegen_value *v, ast_qualified_type *t, int n, ast_qualified_type rty)
{
	return call_named(e, CODEGEN_FIXED_REFERENCE(fn), v, t, n, rty, false);
}

VPRIV codegen_value ld_compare(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	codegen_value v[2] = { a, b };
	ast_qualified_type ts[2] = { t, t };
	int fn, cond, r;
	codegen_value res;

	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: fn = CODEGEN_LIBRARY_EQTF2, cond = C_EQ; break;
	case LEX_TOKEN_EXCLAMATION_EQUAL: fn = CODEGEN_LIBRARY_NETF2, cond = C_NE; break;
	case LEX_TOKEN_LESS: fn = CODEGEN_LIBRARY_LTTF2, cond = C_LT; break;
	case LEX_TOKEN_LESS_EQUAL: fn = CODEGEN_LIBRARY_LETF2, cond = C_LE; break;
	case LEX_TOKEN_GREATER: fn = CODEGEN_LIBRARY_GTTF2, cond = C_GT; break;
	case LEX_TOKEN_GREATER_EQUAL: fn = CODEGEN_LIBRARY_GETF2, cond = C_GE; break;
	default: codegen_fatal_report(e, e->current_location, "unsupported floating point comparison");
	}
	res = ld_call(e, fn, v, ts, 2, codegen_type_primitive(AST_TYPE_INT));
	r = toreg(e, &res);
	opi(e, I_SUBSI, 0, ZR, r, 0);
	cset(e, r, cond);
	return codegen_value_register(r);
}

VPRIV codegen_value f_arith(codegen_context *e, int op, ast_qualified_type ty, codegen_value a, codegen_value b)
{
	int d, ra;
	uint32_t w;

	if (KIND(e->compile_context, ty) == AST_TYPE_FLOAT16 || KIND(e->compile_context, ty) == AST_TYPE_BFLOAT16) {
		a = f_cast(e, a, ty, codegen_type_primitive(AST_TYPE_FLOAT));
		b = f_cast(e, b, ty, codegen_type_primitive(AST_TYPE_FLOAT));
		return f_cast(e, f_arith(e, op, codegen_type_primitive(AST_TYPE_FLOAT), a, b), codegen_type_primitive(AST_TYPE_FLOAT), ty);
	}
	if (codegen_type_is_long_double(e->compile_context, ty)) {
		codegen_value v[2] = { a, b };
		ast_qualified_type ts[2] = { ty, ty };
		int fn = op == LEX_TOKEN_PLUS ? CODEGEN_LIBRARY_ADDTF3 : op == LEX_TOKEN_MINUS ? CODEGEN_LIBRARY_SUBTF3 : op == LEX_TOKEN_STAR ? CODEGEN_LIBRARY_MULTF3 : CODEGEN_LIBRARY_DIVTF3;

		if (op != LEX_TOKEN_PLUS && op != LEX_TOKEN_MINUS && op != LEX_TOKEN_STAR && op != LEX_TOKEN_SLASH)
			codegen_fatal_report(e, e->current_location, "unsupported floating point operator");
		return ld_call(e, fn, v, ts, 2, ty);
	}
	d = fpk(e, ty);
	if (a.kind == CODEGEN_VALUE_CONSTANT && b.kind == CODEGEN_VALUE_CONSTANT) {
		double x = f_val(d, a.integer), y = f_val(d, b.integer);

		switch (op) {
		case LEX_TOKEN_PLUS: return f_const(d, d ? x + y : (double)((float)x + (float)y));
		case LEX_TOKEN_MINUS: return f_const(d, d ? x - y : (double)((float)x - (float)y));
		case LEX_TOKEN_STAR: return f_const(d, d ? x * y : (double)((float)x * (float)y));
		case LEX_TOKEN_SLASH: return f_const(d, d ? x / y : (double)((float)x / (float)y));
		}
	}
	switch (op) {
	case LEX_TOKEN_PLUS: w = 0x1e202800u; break;
	case LEX_TOKEN_MINUS: w = 0x1e203800u; break;
	case LEX_TOKEN_STAR: w = 0x1e200800u; break;
	case LEX_TOKEN_SLASH: w = 0x1e201800u; break;
	default: codegen_fatal_report(e, e->current_location, "unsupported floating point operator");
	}
	f_load(e, d, FV0, &a);
	f_load(e, d, FV1, &b);
	val_free(e, &b);
	ra = a.register_number;
	word(e, w | (unsigned)d << 22 | FV1 << 16 | FV0 << 5 | FV0);
	word(e, f_out(d, ra, FV0));
	return a;
}

/* fcmp a, b (b none: #0.0); the condition for op */
VPRIV int f_cmp(codegen_context *e, int op, int d, codegen_value *a, codegen_value *b)
{
	f_load(e, d, FV0, a);
	if (b) {
		f_load(e, d, FV1, b);
		word(e, 0x1e202000u | (unsigned)d << 22 | FV1 << 16 | FV0 << 5);
	} else {
		word(e, 0x1e202008u | (unsigned)d << 22 | FV0 << 5);
	}
	switch (op) {
	case LEX_TOKEN_EQUAL_EQUAL: return C_EQ;
	case LEX_TOKEN_EXCLAMATION_EQUAL: return C_NE;	/* true when unordered */
	case LEX_TOKEN_LESS: return C_MI;
	case LEX_TOKEN_LESS_EQUAL: return C_LS;
	case LEX_TOKEN_GREATER: return C_GT;
	case LEX_TOKEN_GREATER_EQUAL: return C_GE;
	}
	codegen_fatal_report(e, e->current_location, "unsupported floating point comparison");
}

VPRIV codegen_value f_compare(codegen_context *e, int op, ast_qualified_type t, codegen_value a, codegen_value b)
{
	int d, cond;

	if (KIND(e->compile_context, t) == AST_TYPE_FLOAT16 || KIND(e->compile_context, t) == AST_TYPE_BFLOAT16) {
		a = f_cast(e, a, t, codegen_type_primitive(AST_TYPE_FLOAT));
		b = f_cast(e, b, t, codegen_type_primitive(AST_TYPE_FLOAT));
		return f_compare(e, op, codegen_type_primitive(AST_TYPE_FLOAT), a, b);
	}
	if (codegen_type_is_long_double(e->compile_context, t))
		return ld_compare(e, op, t, a, b);
	d = fpk(e, t);

	if (a.kind == CODEGEN_VALUE_CONSTANT && b.kind == CODEGEN_VALUE_CONSTANT) {
		double x = f_val(d, a.integer), y = f_val(d, b.integer);

		switch (op) {
		case LEX_TOKEN_EQUAL_EQUAL: return codegen_value_constant(x == y);
		case LEX_TOKEN_EXCLAMATION_EQUAL: return codegen_value_constant(x != y);
		case LEX_TOKEN_LESS: return codegen_value_constant(x < y);
		case LEX_TOKEN_LESS_EQUAL: return codegen_value_constant(x <= y);
		case LEX_TOKEN_GREATER: return codegen_value_constant(x > y);
		case LEX_TOKEN_GREATER_EQUAL: return codegen_value_constant(x >= y);
		}
	}
	cond = f_cmp(e, op, d, &a, &b);
	val_free(e, &b);
	cset(e, a.register_number, cond);
	return codegen_value_register(a.register_number);
}

/* x != 0.0 as 0/1 (NaN is true, -0.0 false) */
VPRIV codegen_value f_tobool(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	int d;

	if (KIND(e->compile_context, t) == AST_TYPE_FLOAT16 || KIND(e->compile_context, t) == AST_TYPE_BFLOAT16)
		return f_tobool(e, f_cast(e, v, t, codegen_type_primitive(AST_TYPE_FLOAT)), codegen_type_primitive(AST_TYPE_FLOAT));

	if (codegen_type_is_long_double(e->compile_context, t)) {
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(((u128)v.integer << 1) != 0);
		return ld_compare(e, LEX_TOKEN_EXCLAMATION_EQUAL, t, v, codegen_value_constant(0));
	}
	d = fpk(e, t);

	if (v.kind == CODEGEN_VALUE_CONSTANT)
		return codegen_value_constant(f_val(d, v.integer) != 0);
	f_cmp(e, LEX_TOKEN_EXCLAMATION_EQUAL, d, &v, NULL);
	cset(e, v.register_number, C_NE);
	return v;
}

VPRIV codegen_value f_neg(codegen_context *e, codegen_value v, ast_qualified_type t)
{
	int d;

	if (KIND(e->compile_context, t) == AST_TYPE_FLOAT16 || KIND(e->compile_context, t) == AST_TYPE_BFLOAT16) {
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant(v.integer ^ 0x8000);
		toreg(e, &v);
		opi(e, I_EORI, 0, v.register_number, v.register_number, 0x8000);
		return v;
	}

	if (codegen_type_is_long_double(e->compile_context, t)) {
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return codegen_value_constant((i128)((u128)v.integer ^ (u128)1 << 127));
		toreg2(e, &v, false);
		opi(e, I_EORI, 1, v.register_high, v.register_high, (int64_t)0x8000000000000000ULL);
		return v;
	}
	d = fpk(e, t);

	if (v.kind == CODEGEN_VALUE_CONSTANT)
		return f_const(d, -f_val(d, v.integer));
	f_load(e, d, FV0, &v);
	word(e, 0x1e214000u | (unsigned)d << 22 | FV0 << 5 | FV0);
	word(e, f_out(d, v.register_number, FV0));
	return v;
}

/* conversions to and from long double (one side is long double, the other not bool) */
VPRIV codegen_value ld_cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool ff = codegen_type_is_float(c, from), tf = codegen_type_is_float(c, to);

	if (ff && tf) {
		if (codegen_type_is_long_double(c, from) && codegen_type_is_long_double(c, to))
			return v;
		if (codegen_type_is_long_double(c, to)) {
			if (v.kind == CODEGEN_VALUE_CONSTANT)
				return codegen_value_constant(ld_of_d(f_val(fpk(e, from), v.integer)));
			return ld_call(e, fpk(e, from) ? CODEGEN_LIBRARY_EXTENDDFTF2 : CODEGEN_LIBRARY_EXTENDSFTF2, &v, &from, 1, to);
		}
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return f_const(fpk(e, to), ld_to_d(v.integer));
		return ld_call(e, fpk(e, to) ? CODEGEN_LIBRARY_TRUNCTFDF2 : CODEGEN_LIBRARY_TRUNCTFSF2, &v, &from, 1, to);
	}
	if (tf) {
		uint64_t fs = codegen_type_size(c, from);
		bool sg = !codegen_type_is_pointer(c, from) && codegen_type_is_signed(c, from);
		ast_qualified_type at;
		int fn;

		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			i128 x = codegen_integer_truncate(c, v.integer, from);

			return codegen_value_constant(vsoftfloat_of_integer(x, sg));
		}
		if (fs == 16) {
			fn = sg ? CODEGEN_LIBRARY_FLOATTITF : CODEGEN_LIBRARY_FLOATUNTITF, at = sg ? codegen_type_primitive(AST_TYPE_INT128) : codegen_type_primitive(AST_TYPE_UINT128);
		} else if (fs == 8) {
			fn = sg ? CODEGEN_LIBRARY_FLOATDITF : CODEGEN_LIBRARY_FLOATUNDITF, at = sg ? codegen_type_primitive(AST_TYPE_LONG) : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG);
		} else {
			toreg(e, &v);
			if (fs < 4)
				extend(e, v.register_number, v.register_number, (int)fs, sg);
			fn = sg ? CODEGEN_LIBRARY_FLOATSITF : CODEGEN_LIBRARY_FLOATUNSITF, at = sg ? codegen_type_primitive(AST_TYPE_INT) : codegen_type_primitive(AST_TYPE_UNSIGNED_INT);
		}
		return ld_call(e, fn, &v, &at, 1, to);
	}
	{
		uint64_t ts = codegen_type_size(c, to);
		bool sg = !codegen_type_is_pointer(c, to) && codegen_type_is_signed(c, to);
		int fn;
		ast_qualified_type rt;
		codegen_value r;

		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			double x = ld_to_d(v.integer);

			return codegen_value_constant(codegen_integer_truncate(c, sg ? (i128)x : (i128)(u128)x, to));
		}
		if (ts == 16)
			return ld_call(e, sg ? CODEGEN_LIBRARY_FIXTFTI : CODEGEN_LIBRARY_FIXUNSTFTI, &v, &from, 1, to);
		fn = ts == 8 ? (sg ? CODEGEN_LIBRARY_FIXTFDI : CODEGEN_LIBRARY_FIXUNSTFDI) : (sg ? CODEGEN_LIBRARY_FIXTFSI : CODEGEN_LIBRARY_FIXUNSTFSI);
		rt = ts == 8 ? (sg ? codegen_type_primitive(AST_TYPE_LONG) : codegen_type_primitive(AST_TYPE_UNSIGNED_LONG)) : (sg ? codegen_type_primitive(AST_TYPE_INT) : codegen_type_primitive(AST_TYPE_UNSIGNED_INT));
		r = ld_call(e, fn, &v, &from, 1, rt);
		if (ts < 4) {
			toreg(e, &r);
			extend(e, r.register_number, r.register_number, (int)ts, sg);
		}
		return r;
	}
}

VPRIV codegen_value f_cast(codegen_context *e, codegen_value v, ast_qualified_type from, ast_qualified_type to)
{
	cc *c = e->compile_context;
	bool ff = codegen_type_is_float(c, from), tf = codegen_type_is_float(c, to);

	if (v.kind == CODEGEN_VALUE_NONE)
		return v;
	if (KIND(c, from) == AST_TYPE_BFLOAT16) {
		if (KIND(c, to) == AST_TYPE_BFLOAT16) return v;
		v = call_named(e, CODEGEN_FIXED_REFERENCE(CODEGEN_LIBRARY_EXTENDBFSF2), &v, &from, 1, codegen_type_primitive(AST_TYPE_FLOAT), false);
		return f_cast(e, v, codegen_type_primitive(AST_TYPE_FLOAT), to);
	}
	if (KIND(c, to) == AST_TYPE_BFLOAT16) {
		if (KIND(c, from) == AST_TYPE_FLOAT16)
			v = f_cast(e, v, from, codegen_type_primitive(AST_TYPE_FLOAT)), from = codegen_type_primitive(AST_TYPE_FLOAT);
		if (!ff)
			v = f_cast(e, v, from, codegen_type_primitive(AST_TYPE_DOUBLE)), from = codegen_type_primitive(AST_TYPE_DOUBLE);
		return call_named(e, CODEGEN_FIXED_REFERENCE(codegen_type_is_long_double(c, from) ? CODEGEN_LIBRARY_TRUNCTFBF2 : KIND(c, from) == AST_TYPE_FLOAT ? CODEGEN_LIBRARY_TRUNCSFBF2 : CODEGEN_LIBRARY_TRUNCDFBF2), &v, &from, 1, to, false);
	}
	if (KIND(c, from) == AST_TYPE_FLOAT16 || KIND(c, to) == AST_TYPE_FLOAT16) {
		bool fh = KIND(c, from) == AST_TYPE_FLOAT16;
		ast_qualified_type wide = fh ? to : from;

		if (from == to)
			return v;
		if (fh && (!tf || codegen_type_is_long_double(c, to)))
			return f_cast(e, f_cast(e, v, from, codegen_type_primitive(AST_TYPE_FLOAT)), codegen_type_primitive(AST_TYPE_FLOAT), to);
		if (!fh && codegen_type_is_long_double(c, from))
			return call_named(e, CODEGEN_FIXED_REFERENCE(CODEGEN_LIBRARY_TRUNCTFHF2), &v, &from, 1, to, false);
		if (!fh && !ff)
			return f_cast(e, f_cast(e, v, from, codegen_type_primitive(AST_TYPE_DOUBLE)), codegen_type_primitive(AST_TYPE_DOUBLE), to);
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return fh ? f_const_t(e, to, h_to_d((uint16_t)v.integer)) : codegen_value_constant(vsoftfloat_to_half(vsoftfloat_of_double(f_val(fpk(e, from), v.integer))));
		{
			bool d = KIND(c, wide) == AST_TYPE_DOUBLE;
			uint32_t op = fh ? (d ? 0x1ee2c000u : 0x1ee24000u) : (d ? 0x1e63c000u : 0x1e23c000u);

			f_load(e, !fh && d, FV0, &v);
			word(e, op | FV0 << 5 | FV0);
			word(e, f_out(fh && d, v.register_number, FV0));
			return v;
		}
	}
	if (KIND(c, to) == AST_TYPE_BOOL)
		return f_tobool(e, v, from);
	if (codegen_type_is_long_double(c, from) || codegen_type_is_long_double(c, to))
		return ld_cast(e, v, from, to);
	if (ff && tf) {
		int fd = fpk(e, from), td = fpk(e, to);

		if (fd == td)
			return v;
		if (v.kind == CODEGEN_VALUE_CONSTANT)
			return f_const(td, f_val(fd, v.integer));
		f_load(e, fd, FV0, &v);
		word(e, (fd ? 0x1e624000u : 0x1e22c000u) | FV0 << 5 | FV0);	/* fcvt s<-d / d<-s */
		word(e, f_out(td, v.register_number, FV0));
		return v;
	}
	if (tf) {
		/* integer -> floating */
		int td = fpk(e, to);
		uint64_t fs = codegen_type_size(c, from);
		bool sg = !codegen_type_is_pointer(c, from) && codegen_type_is_signed(c, from);

		if (fs == 16) {
			/* __floattidf & co. */
			codegen_value r;
			ast_qualified_type ft = sg ? codegen_type_primitive(AST_TYPE_INT128) : codegen_type_primitive(AST_TYPE_UINT128);

			if (v.kind == CODEGEN_VALUE_CONSTANT)
				return f_const(td, td ? (sg ? (double)v.integer : (double)(u128)v.integer) :
					       (sg ? (double)(float)v.integer : (double)(float)(u128)v.integer));
			r = call_named(e, CODEGEN_FIXED_REFERENCE(sg ? (td ? CODEGEN_LIBRARY_FLOATTIDF : CODEGEN_LIBRARY_FLOATTISF) : (td ? CODEGEN_LIBRARY_FLOATUNTIDF : CODEGEN_LIBRARY_FLOATUNTISF)),
				       &v, &ft, 1, to, false);
			return r;
		}
		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			i128 x = codegen_integer_truncate(c, v.integer, from);

			/* Round directly to float: going through double loses the
			 * sticky bit for integers just above a float midpoint. */
			return f_const(td, td ? (sg ? (double)(int64_t)x : (double)(uint64_t)x) :
				       (sg ? (double)(float)(int64_t)x : (double)(float)(uint64_t)x));
		}
		toreg(e, &v);
		if (fs < 4)
			extend(e, v.register_number, v.register_number, (int)fs, sg);
		/* scvtf/ucvtf v16, x/w */
		word(e, (fs == 8 ? 0x9e220000u : 0x1e220000u) | (sg ? 0 : 0x10000u) | (unsigned)td << 22 | (unsigned)v.register_number << 5 | FV0);
		word(e, f_out(td, v.register_number, FV0));
		return v;
	}
	{
		/* floating -> integer (fcvtzs/fcvtzu, x or w) */
		int fd = fpk(e, from);
		uint64_t ts = codegen_type_size(c, to);
		bool sg = !codegen_type_is_pointer(c, to) && codegen_type_is_signed(c, to);

		if (ts == 16) {
			/* __fixdfti & co. */
			if (v.kind == CODEGEN_VALUE_CONSTANT) {
				double x = f_val(fd, v.integer);

				return codegen_value_constant(sg ? (i128)x : (i128)(u128)x);
			}
			return call_named(e, CODEGEN_FIXED_REFERENCE(sg ? (fd ? CODEGEN_LIBRARY_FIXDFTI : CODEGEN_LIBRARY_FIXSFTI) : (fd ? CODEGEN_LIBRARY_FIXUNSDFTI : CODEGEN_LIBRARY_FIXUNSSFTI)),
					  &v, &from, 1, to, false);
		}
		if (v.kind == CODEGEN_VALUE_CONSTANT) {
			double x = f_val(fd, v.integer);

			return codegen_value_constant(codegen_integer_truncate(c, sg ? (i128)(int64_t)x : (i128)(uint64_t)x, to));
		}
		f_load(e, fd, FV0, &v);
		word(e, (ts == 8 ? 0x9e380000u : 0x1e380000u) | (sg ? 0 : 0x10000u) | (unsigned)fd << 22 | FV0 << 5 | (unsigned)v.register_number);
		if (ts < 4)
			extend(e, v.register_number, v.register_number, (int)ts, sg);
		return v;
	}
}
