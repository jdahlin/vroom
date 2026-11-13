/* vcc front end: builtins that are a library call in disguise (strncmp, narrowed and fast math, fabs/copysign); see builtin-table.c for where a builtin's other facts live */
#include "ccimpl.h"

/* A zero bound reads neither string, but all argument expressions still run. */
VPRIV uint32_t fold_strncmp_zero(cc *c, const vec *args, uint32_t loc)
{
	cval cv;

	if (args->n != 3 || !cc_eval(c, args->v[2], &cv) || cv.kind != CV_INT || cv.i)
		return 0;
	uint32_t n = mk_int(c, 0, prim(TY_INT), loc);
	for (int i = 2; i >= 0; i--)
		n = with_pre(c, args->v[i], n);
	return n;
}

/* Rounding a value promoted from float has an exact float result. Unlike
 * transcendental functions, narrowing these operations cannot double-round. */
VPRIV uint32_t narrow_math_call(cc *c, const char *name, size_t len, uint32_t arg, uint32_t loc)
{
	if (!c->o->opt || N(arg)->op != N_CAST || N(arg)->aux != CK_F2F ||
	    KIND(c, N(arg)->ty) != TY_DOUBLE || KIND(c, N(N(arg)->a)->ty) != TY_FLOAT)
		return 0;
	if (!((len == 5 && (!memcmp(name, "floor", 5) || !memcmp(name, "trunc", 5) || !memcmp(name, "round", 5))) ||
	      (len == 4 && !memcmp(name, "ceil", 4))))
		return 0;
	cval cv;
	if (cc_eval(c, arg, &cv))
		return 0;	/* leave constant arguments to the existing libm fold */
	char target[32] = "__builtin_";
	memcpy(target + 10, name, len);
	target[10 + len] = 'f';
	target[11 + len] = 0;
	for (const builtin *b = builtin_all(); b->name; b++) {
		if (strcmp(b->name, target))
			continue;
		qty ft = builtin_type(c, b);
		uint32_t s = sym_new(c, S_BUILTIN, 0, ft, loc);
		c->syms[s].val = b - builtin_all();
		uint32_t fn = mk(c, N_VAR, ft, loc);
		N(fn)->a = s;
		vec args;
		vinit(&args);
		vpush(c, &args, N(arg)->a);
		uint32_t n = mk(c, N_CALL, prim(TY_FLOAT), loc);
		N(n)->a = decay(c, fn);
		N(n)->b = vfreeze(c, &args);
		N(n)->c = 1;
		return convert(c, n, prim(TY_DOUBLE));
	}
	return 0;
}

/* fabs/copysign are representation operations: integer masks preserve both
 * signaling NaNs and signed zero. Anonymous locals evaluate each operand once. */
VPRIV uint32_t fp_sign_call(cc *c, const char *name, size_t len, qty ft, const vec *args, uint32_t loc)
{
	bool copy = (len == 8 && !memcmp(name, "copysign", 8)) ||
		    (len == 9 && (!memcmp(name, "copysignf", 9) || !memcmp(name, "copysignl", 9)));
	bool abs = (len == 4 && !memcmp(name, "fabs", 4)) ||
		   (len == 5 && (!memcmp(name, "fabsf", 5) || !memcmp(name, "fabsl", 5)));
	if (!copy && !abs) return 0;
	bool single = name[len - 1] == 'f', extended = name[len - 1] == 'l';
	qty t = prim(extended ? TY_LDOUBLE : single ? TY_FLOAT : TY_DOUBLE);
	if ((T(c, ft)->flags & (TF_NOPROTO | TF_VARIADIC)) || args->n != (copy ? 2 : 1) ||
	    !compatible(c, ft, sig_func_type(c, copy ? (extended ? "DDD" : single ? "fff" : "ddd") : (extended ? "DD" : single ? "ff" : "dd"))))
		return 0;
	cval a, b;
	if (!elementwise_abs_effects(c, args->v[0], true) && cc_eval(c, args->v[0], &a) && a.kind == CV_FLT &&
	    (!copy || (!elementwise_abs_effects(c, args->v[1], true) && cc_eval(c, args->v[1], &b) && b.kind == CV_FLT)))
	{
		u128 q = a.ld ? a.q : q_of_d(a.f);
		u128 sign = copy ? (b.ld ? b.q : q_of_d(b.f)) & ((u128)1 << 127) : 0;
		/* Keep the original constant node: converting through host double
		   would quiet or canonicalize a signaling NaN payload. */
		return ((q ^ sign) >> 127) ? neg(c, args->v[0]) : args->v[0];
	}
	if (extended) return 0;
	if (c->o->target != PP_LINUX_X86_64 && c->o->target != PP_LINUX_AARCH64)
		return 0;
	if (c->o->target == PP_LINUX_X86_64 ?
	    !x86_feature_has(cc_x86_features(c, c->fn), single ? X86_FEATURE_SSE : X86_FEATURE_SSE2) : c->o->general_regs_only) {
		cc_error(c, loc, "%s floating point is not supported", "-mgeneral-regs-only:");
		return err_node(c, loc);
	}
	qty bits = prim(single ? TY_UINT : TY_ULLONG);
	i128 sign = (i128)1 << (single ? 31 : 63);
	uint32_t tmp[2], raw[2], pre[2];
	for (uint32_t i = 0; i < args->n; i++) {
		uint32_t s = sym_new(c, S_VAR, 0, t, loc);
		c->syms[s].sc = SC_AUTO;
		tmp[i] = mk(c, N_VAR, t, loc); N(tmp[i])->a = s; N(tmp[i])->flags = NF_LVALUE;
		pre[i] = mk2(c, N_ASSIGN, t, loc, tmp[i], args->v[i]);
		uint32_t ptr = cast_node(c, addr_of(c, tmp[i], loc), ty_ptr(c, bits), CK_P2P);
		raw[i] = mk(c, N_DEREF, bits, loc); N(raw[i])->a = ptr; N(raw[i])->flags = NF_LVALUE;
	}
	uint32_t value = mk_bin(c, T_AMP, bits, rvalue(c, raw[0]), mk_int(c, sign - 1, bits, loc), loc);
	if (copy) {
		uint32_t source = mk_bin(c, T_AMP, bits, rvalue(c, raw[1]), mk_int(c, sign, bits, loc), loc);
		value = mk_bin(c, T_PIPE, bits, value, source, loc);
	}
	uint32_t result = with_pre(c, mk2(c, N_ASSIGN, bits, loc, raw[0], value), rvalue(c, tmp[0]));
	if (copy) result = with_pre(c, pre[1], result);
	result = with_pre(c, pre[0], result);
	/* Retain fabs facts after its call has become representation operations.
	   This describes only the result; the comma operands still execute. */
	if (abs) N(result)->aux |= NC_FP_NONNEG;
	return result;
}

/* Full fast-math permits roots and short multiplication chains without
 * errno or strict rounding/exception preservation. Library identity is checked
 * by the caller; builtin spelling is an explicit request for these semantics. */
VPRIV uint32_t fast_math_call(cc *c, const char *name, size_t len, qty ft, const vec *args, uint32_t loc)
{
	if (!c->o->fast_math || (c->o->target != PP_LINUX_X86_64 && c->o->target != PP_LINUX_AARCH64)) return 0;
	bool power = len == 3 && !memcmp(name, "pow", 3);
	bool root = (len == 4 && !memcmp(name, "sqrt", 4)) || (len == 5 && !memcmp(name, "sqrtf", 5));
	if (!power && !root) return 0;
	bool single = !power && len == 5;
	qty t = prim(single ? TY_FLOAT : TY_DOUBLE);
	if ((T(c, ft)->flags & (TF_NOPROTO | TF_VARIADIC)) || args->n != (power ? 2 : 1) ||
	    !compatible(c, ft, sig_func_type(c, power ? "ddd" : single ? "ff" : "dd"))) return 0;
	cval exponent;
	double e = 0.5;
	if (power) {
		if (elementwise_abs_effects(c, args->v[1], true) || !cc_eval(c, args->v[1], &exponent) ||
		    exponent.kind != CV_FLT || exponent.ld) return 0;
		e = exponent.f;
		if (e != 0.25 && e != 0.5 && !(e >= -64 && e <= 64 && e == (int)e)) return 0;
	}
	if (c->o->target == PP_LINUX_X86_64 ?
	    !x86_feature_has(cc_x86_features(c, c->fn), single ? X86_FEATURE_SSE : X86_FEATURE_SSE2) : c->o->general_regs_only) {
		cc_error(c, loc, "%s floating point is not supported", "-mgeneral-regs-only:");
		return err_node(c, loc);
	}
	if (e == 0.25 || e == 0.5) {
		vec one; vinit(&one); vpush(c, &one, args->v[0]);
		return mk_builtin(c, e == 0.25 ? BI_FAST_ROOT4 : BI_FAST_SQRT, t, loc, &one);
	}
	unsigned n = e < 0 ? (unsigned)-e : (unsigned)e;
	uint32_t pre[8], np = 0, value = args->v[0], result = mk_flt(c, 1.0, t, loc);
	for (;;) {
		/* Every reusable factor is materialized, including the original base:
		 * volatile reads and calls occur once even for a zero exponent. */
		uint32_t s = sym_new(c, S_VAR, 0, t, loc);
		c->syms[s].sc = SC_AUTO;
		uint32_t tmp = mk(c, N_VAR, t, loc); N(tmp)->a = s; N(tmp)->flags = NF_LVALUE;
		pre[np++] = mk2(c, N_ASSIGN, t, loc, tmp, value);
		value = rvalue(c, tmp);
		if (n & 1) result = mk_bin(c, T_STAR, t, result, value, loc);
		n >>= 1;
		if (!n) break;
		value = mk_bin(c, T_STAR, t, value, value, loc);
	}
	if (e < 0) result = mk_bin(c, T_SLASH, t, mk_flt(c, 1.0, t, loc), result, loc);
	while (np) result = with_pre(c, pre[--np], result);
	return result;
}
