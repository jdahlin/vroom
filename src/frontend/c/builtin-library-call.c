/* vcc front end: builtins that are a library call in disguise (strncmp, narrowed and fast math, fabs/copysign); see builtin-table.c for where a builtin's other facts live */
#include "cc-internal.h"

/* A zero bound reads neither string, but all argument expressions still run. */
CC_PRIVATE uint32_t cc_builtin_strncmp_zero_fold(cc *c, const cc_id_list *args, uint32_t loc)
{
	cc_value cv;

	if (args->n != 3 || !cc_eval(c, args->v[2], &cv) || cv.kind != CC_VALUE_INTEGER || cv.i)
		return 0;
	uint32_t n = cc_node_integer_new(c, 0, cc_type_basic(AST_TYPE_INT), loc);
	for (int i = 2; i >= 0; i--)
		n = cc_lowering_sequence(c, args->v[i], n);
	return n;
}

/* Rounding a value promoted from float has an exact float result. Unlike
 * transcendental functions, narrowing these operations cannot double-round. */
CC_PRIVATE uint32_t cc_builtin_narrow_math_build(cc *c, const char *name, size_t len, uint32_t arg, uint32_t loc)
{
	if (!c->o->opt || AST_NODE(arg)->op != AST_NODE_CAST || AST_NODE(arg)->aux != AST_CAST_FLOAT_TO_FLOAT ||
	    AST_TYPE_KIND(c, AST_NODE(arg)->ty) != AST_TYPE_DOUBLE || AST_TYPE_KIND(c, AST_NODE(AST_NODE(arg)->a)->ty) != AST_TYPE_FLOAT)
		return 0;
	if (!((len == 5 && (!memcmp(name, "floor", 5) || !memcmp(name, "trunc", 5) || !memcmp(name, "round", 5))) ||
	      (len == 4 && !memcmp(name, "ceil", 4))))
		return 0;
	cc_value cv;
	if (cc_eval(c, arg, &cv))
		return 0;	/* leave constant arguments to the existing libm fold */
	char target[32] = "__builtin_";
	memcpy(target + 10, name, len);
	target[10 + len] = 'f';
	target[11 + len] = 0;
	for (const cc_builtin *b = cc_builtin_table_get(); b->name; b++) {
		if (strcmp(b->name, target))
			continue;
		ast_qualified_type ft = cc_builtin_type(c, b);
		uint32_t s = cc_symbol_new(c, AST_SYMBOL_BUILTIN, 0, ft, loc);
		c->syms[s].val = b - cc_builtin_table_get();
		uint32_t fn = cc_node_new(c, AST_NODE_VARIABLE, ft, loc);
		AST_NODE(fn)->a = s;
		cc_id_list args;
		cc_id_list_init(&args);
		cc_id_list_append(c, &args, AST_NODE(arg)->a);
		uint32_t n = cc_node_new(c, AST_NODE_CALL, cc_type_basic(AST_TYPE_FLOAT), loc);
		AST_NODE(n)->a = cc_conversion_decay(c, fn);
		AST_NODE(n)->b = cc_id_list_freeze(c, &args);
		AST_NODE(n)->c = 1;
		return cc_conversion_apply(c, n, cc_type_basic(AST_TYPE_DOUBLE));
	}
	return 0;
}

/* fabs/copysign are representation operations: integer masks preserve both
 * signaling NaNs and signed zero. Anonymous locals evaluate each operand once. */
CC_PRIVATE uint32_t cc_builtin_fp_sign_build(cc *c, const char *name, size_t len, ast_qualified_type ft, const cc_id_list *args, uint32_t loc)
{
	bool copy = (len == 8 && !memcmp(name, "copysign", 8)) ||
		    (len == 9 && (!memcmp(name, "copysignf", 9) || !memcmp(name, "copysignl", 9)));
	bool abs = (len == 4 && !memcmp(name, "fabs", 4)) ||
		   (len == 5 && (!memcmp(name, "fabsf", 5) || !memcmp(name, "fabsl", 5)));
	if (!copy && !abs) return 0;
	bool single = name[len - 1] == 'f', extended = name[len - 1] == 'l';
	ast_qualified_type t = cc_type_basic(extended ? AST_TYPE_LONG_DOUBLE : single ? AST_TYPE_FLOAT : AST_TYPE_DOUBLE);
	if ((AST_TYPE(c, ft)->flags & (AST_TYPE_FLAG_NO_PROTOTYPE | AST_TYPE_FLAG_VARIADIC)) || args->n != (copy ? 2 : 1) ||
	    !cc_type_is_compatible(c, ft, cc_builtin_signature_function_type(c, copy ? (extended ? "DDD" : single ? "fff" : "ddd") : (extended ? "DD" : single ? "ff" : "dd"))))
		return 0;
	cc_value a, b;
	if (!cc_builtin_has_effects(c, args->v[0], true) && cc_eval(c, args->v[0], &a) && a.kind == CC_VALUE_FLOAT &&
	    (!copy || (!cc_builtin_has_effects(c, args->v[1], true) && cc_eval(c, args->v[1], &b) && b.kind == CC_VALUE_FLOAT)))
	{
		u128 q = a.ld ? a.q : vsoftfloat_of_double(a.f);
		u128 sign = copy ? (b.ld ? b.q : vsoftfloat_of_double(b.f)) & ((u128)1 << 127) : 0;
		/* Keep the original constant node: converting through host double
		   would quiet or canonicalize a signaling NaN payload. */
		return ((q ^ sign) >> 127) ? neg(c, args->v[0]) : args->v[0];
	}
	if (extended) return 0;
	if (!CC_X86_64(c) && c->o->target != PP_LINUX_AARCH64)
		return 0;
	if (CC_X86_64(c) ?
	    !x86_feature_has(cc_x86_features_get(c, c->fn), single ? X86_FEATURE_SSE : X86_FEATURE_SSE2) : c->o->general_regs_only) {
		cc_report_error(c, loc, "%s floating point is not supported", "-mgeneral-regs-only:");
		return cc_node_error_new(c, loc);
	}
	ast_qualified_type bits = cc_type_basic(single ? AST_TYPE_UNSIGNED_INT : AST_TYPE_UNSIGNED_LONG_LONG);
	i128 sign = (i128)1 << (single ? 31 : 63);
	uint32_t tmp[2], raw[2], pre[2];
	for (uint32_t i = 0; i < args->n; i++) {
		uint32_t s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
		c->syms[s].sc = AST_STORAGE_AUTO;
		tmp[i] = cc_node_new(c, AST_NODE_VARIABLE, t, loc); AST_NODE(tmp[i])->a = s; AST_NODE(tmp[i])->flags = AST_NODE_FLAG_LVALUE;
		pre[i] = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, tmp[i], args->v[i]);
		uint32_t ptr = cc_cast_insert(c, cc_address_of_build(c, tmp[i], loc), cc_type_pointer(c, bits), AST_CAST_POINTER_TO_POINTER);
		raw[i] = cc_node_new(c, AST_NODE_DEREFERENCE, bits, loc); AST_NODE(raw[i])->a = ptr; AST_NODE(raw[i])->flags = AST_NODE_FLAG_LVALUE;
	}
	uint32_t value = cc_node_binary_new(c, LEX_TOKEN_AMPERSAND, bits, cc_node_rvalue(c, raw[0]), cc_node_integer_new(c, sign - 1, bits, loc), loc);
	if (copy) {
		uint32_t source = cc_node_binary_new(c, LEX_TOKEN_AMPERSAND, bits, cc_node_rvalue(c, raw[1]), cc_node_integer_new(c, sign, bits, loc), loc);
		value = cc_node_binary_new(c, LEX_TOKEN_PIPE, bits, value, source, loc);
	}
	uint32_t result = cc_lowering_sequence(c, cc_node_new_operands(c, AST_NODE_ASSIGN, bits, loc, raw[0], value), cc_node_rvalue(c, tmp[0]));
	if (copy) result = cc_lowering_sequence(c, pre[1], result);
	result = cc_lowering_sequence(c, pre[0], result);
	/* Retain fabs facts after its call has become representation operations.
	   This describes only the result; the comma operands still execute. */
	if (abs) AST_NODE(result)->aux |= AST_NODE_COMMA_FP_NONNEGATIVE;
	return result;
}

/* Full fast-math permits roots and short multiplication chains without
 * errno or strict rounding/exception preservation. Library identity is checked
 * by the caller; builtin spelling is an explicit request for these semantics. */
CC_PRIVATE uint32_t cc_builtin_fast_math_build(cc *c, const char *name, size_t len, ast_qualified_type ft, const cc_id_list *args, uint32_t loc)
{
	if (!c->o->fast_math || (!CC_X86_64(c) && c->o->target != PP_LINUX_AARCH64)) return 0;
	bool power = len == 3 && !memcmp(name, "pow", 3);
	bool root = (len == 4 && !memcmp(name, "sqrt", 4)) || (len == 5 && !memcmp(name, "sqrtf", 5));
	if (!power && !root) return 0;
	bool single = !power && len == 5;
	ast_qualified_type t = cc_type_basic(single ? AST_TYPE_FLOAT : AST_TYPE_DOUBLE);
	if ((AST_TYPE(c, ft)->flags & (AST_TYPE_FLAG_NO_PROTOTYPE | AST_TYPE_FLAG_VARIADIC)) || args->n != (power ? 2 : 1) ||
	    !cc_type_is_compatible(c, ft, cc_builtin_signature_function_type(c, power ? "ddd" : single ? "ff" : "dd"))) return 0;
	cc_value exponent;
	double e = 0.5;
	if (power) {
		if (cc_builtin_has_effects(c, args->v[1], true) || !cc_eval(c, args->v[1], &exponent) ||
		    exponent.kind != CC_VALUE_FLOAT || exponent.ld) return 0;
		e = exponent.f;
		if (e != 0.25 && e != 0.5 && !(e >= -64 && e <= 64 && e == (int)e)) return 0;
	}
	if (CC_X86_64(c) ?
	    !x86_feature_has(cc_x86_features_get(c, c->fn), single ? X86_FEATURE_SSE : X86_FEATURE_SSE2) : c->o->general_regs_only) {
		cc_report_error(c, loc, "%s floating point is not supported", "-mgeneral-regs-only:");
		return cc_node_error_new(c, loc);
	}
	if (e == 0.25 || e == 0.5) {
		cc_id_list one; cc_id_list_init(&one); cc_id_list_append(c, &one, args->v[0]);
		return cc_builtin_node_new(c, e == 0.25 ? CC_BUILTIN_FAST_ROOT4 : CC_BUILTIN_FAST_SQRT, t, loc, &one);
	}
	unsigned n = e < 0 ? (unsigned)-e : (unsigned)e;
	uint32_t pre[8], np = 0, value = args->v[0], result = cc_node_float_new(c, 1.0, t, loc);
	for (;;) {
		/* Every reusable factor is materialized, including the original base:
		 * volatile reads and calls occur once even for a zero exponent. */
		uint32_t s = cc_symbol_new(c, AST_SYMBOL_VARIABLE, 0, t, loc);
		c->syms[s].sc = AST_STORAGE_AUTO;
		uint32_t tmp = cc_node_new(c, AST_NODE_VARIABLE, t, loc); AST_NODE(tmp)->a = s; AST_NODE(tmp)->flags = AST_NODE_FLAG_LVALUE;
		pre[np++] = cc_node_new_operands(c, AST_NODE_ASSIGN, t, loc, tmp, value);
		value = cc_node_rvalue(c, tmp);
		if (n & 1) result = cc_node_binary_new(c, LEX_TOKEN_STAR, t, result, value, loc);
		n >>= 1;
		if (!n) break;
		value = cc_node_binary_new(c, LEX_TOKEN_STAR, t, value, value, loc);
	}
	if (e < 0) result = cc_node_binary_new(c, LEX_TOKEN_SLASH, t, cc_node_float_new(c, 1.0, t, loc), result, loc);
	while (np) result = cc_lowering_sequence(c, pre[--np], result);
	return result;
}
