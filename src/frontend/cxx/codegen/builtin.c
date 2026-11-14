/* C++ code generation: the builtins (__builtin_*) and the library functions behind them */
#include "codegen.h"

/* the builtins that are a C library function (or operator new/delete) by another name */
static const struct cxx_builtin_function {
	const char *name, *symbol;
	char ret, params[4];	/* v void, p void *, q const void *, s const char *, z size_t, i int, f float, d double, c char */
} cxx_builtin_functions[] = {
	{ "__builtin_memcpy", "memcpy", 'p', "pqz" }, { "__builtin_memmove", "memmove", 'p', "pqz" },
	{ "__builtin_memset", "memset", 'p', "piz" }, { "__builtin_memcmp", "memcmp", 'i', "qqz" },
	{ "__builtin_strlen", "strlen", 'z', "s" }, { "__builtin_memchr", "memchr", 'p', "qiz" },
	{ "__builtin_strcmp", "strcmp", 'i', "ss" }, { "__builtin_unreachable", "abort", 'v', "" },
	{ "__builtin_trap", "abort", 'v', "" }, { "__builtin_abort", "abort", 'v', "" },
	{ "__builtin_operator_new", "_Znwm", 'p', "z" }, { "__builtin_operator_delete", "_ZdlPv", 'v', "p" },
	{ "__builtin_operator_new", "_ZnwmSt11align_val_t", 'p', "zz" }, { "__builtin_operator_delete", "_ZdlPvm", 'v', "pz" },
	{ "__builtin_operator_delete", "_ZdlPvmSt11align_val_t", 'v', "pzz" },
	/* (new and delete expressions: cxx_gen_new, cxx_gen_delete) */
	{ "__vcc_new_array", "_Znam", 'p', "z" }, { "__vcc_delete_array", "_ZdaPv", 'v', "p" },
	{ "__builtin_sin", "sin", 'd', "d" }, { "__builtin_sinf", "sinf", 'f', "f" },
	{ "__builtin_cos", "cos", 'd', "d" }, { "__builtin_cosf", "cosf", 'f', "f" },
	{ "__builtin_tan", "tan", 'd', "d" }, { "__builtin_tanf", "tanf", 'f', "f" },
	{ "__builtin_asin", "asin", 'd', "d" }, { "__builtin_asinf", "asinf", 'f', "f" },
	{ "__builtin_acos", "acos", 'd', "d" }, { "__builtin_acosf", "acosf", 'f', "f" },
	{ "__builtin_atan", "atan", 'd', "d" }, { "__builtin_atanf", "atanf", 'f', "f" },
	{ "__builtin_sinh", "sinh", 'd', "d" }, { "__builtin_sinhf", "sinhf", 'f', "f" },
	{ "__builtin_cosh", "cosh", 'd', "d" }, { "__builtin_coshf", "coshf", 'f', "f" },
	{ "__builtin_tanh", "tanh", 'd', "d" }, { "__builtin_tanhf", "tanhf", 'f', "f" },
	{ "__builtin_exp", "exp", 'd', "d" }, { "__builtin_expf", "expf", 'f', "f" },
	{ "__builtin_log", "log", 'd', "d" }, { "__builtin_logf", "logf", 'f', "f" },
	{ "__builtin_log10", "log10", 'd', "d" }, { "__builtin_log10f", "log10f", 'f', "f" },
	{ "__builtin_log2", "log2", 'd', "d" }, { "__builtin_log2f", "log2f", 'f', "f" },
	{ "__builtin_sqrt", "sqrt", 'd', "d" }, { "__builtin_sqrtf", "sqrtf", 'f', "f" },
	{ "__builtin_cbrt", "cbrt", 'd', "d" }, { "__builtin_cbrtf", "cbrtf", 'f', "f" },
	{ "__builtin_fabs", "fabs", 'd', "d" }, { "__builtin_fabsf", "fabsf", 'f', "f" },
	{ "__builtin_floor", "floor", 'd', "d" }, { "__builtin_floorf", "floorf", 'f', "f" },
	{ "__builtin_ceil", "ceil", 'd', "d" }, { "__builtin_ceilf", "ceilf", 'f', "f" },
	{ "__builtin_round", "round", 'd', "d" }, { "__builtin_roundf", "roundf", 'f', "f" },
	{ "__builtin_trunc", "trunc", 'd', "d" }, { "__builtin_truncf", "truncf", 'f', "f" },
	{ "__builtin_exp2", "exp2", 'd', "d" }, { "__builtin_exp2f", "exp2f", 'f', "f" },
	{ "__builtin_expm1", "expm1", 'd', "d" }, { "__builtin_expm1f", "expm1f", 'f', "f" },
	{ "__builtin_log1p", "log1p", 'd', "d" }, { "__builtin_log1pf", "log1pf", 'f', "f" },
	{ "__builtin_pow", "pow", 'd', "dd" }, { "__builtin_powf", "powf", 'f', "ff" },
	{ "__builtin_fmod", "fmod", 'd', "dd" }, { "__builtin_fmodf", "fmodf", 'f', "ff" },
	{ "__builtin_atan2", "atan2", 'd', "dd" }, { "__builtin_atan2f", "atan2f", 'f', "ff" },
	{ "__builtin_hypot", "hypot", 'd', "dd" }, { "__builtin_hypotf", "hypotf", 'f', "ff" },
	{ "__builtin_copysign", "copysign", 'd', "dd" }, { "__builtin_copysignf", "copysignf", 'f', "ff" },
	{ "__builtin_fmin", "fmin", 'd', "dd" }, { "__builtin_fminf", "fminf", 'f', "ff" },
	{ "__builtin_fmax", "fmax", 'd', "dd" }, { "__builtin_fmaxf", "fmaxf", 'f', "ff" },
	{ "__builtin_nan", "nan", 'd', "s" }, { "__builtin_nanf", "nanf", 'f', "s" },
	{ "__builtin_char_memchr", "memchr", 'p', "qiz" }, { "__builtin_strchr", "strchr", 'p', "si" },
	{ "__builtin_strncmp", "strncmp", 'i', "ssz" }, { "__builtin_bcmp", "memcmp", 'i', "qqz" },
	{ "__builtin_free", "free", 'v', "p" }, { "__builtin_malloc", "malloc", 'p', "z" },
};

VPRIV qty cxx_builtin_code_type(cxx *c, char code)
{
	cc *a = c->ast;

	switch (code) {
	case 'p': return cc_type_pointer(a, cc_type_basic(TY_VOID));
	case 'q': return cc_type_pointer(a, cc_type_qualify(a, cc_type_basic(TY_VOID), Q_CONST));
	case 's': return cc_type_pointer(a, cc_type_qualify(a, cc_type_basic(TY_CHAR), Q_CONST));
	case 'z': return cc_type_basic(TY_ULONG);
	case 'i': return cc_type_basic(TY_INT);
	case 'f': return cc_type_basic(TY_FLOAT);
	case 'd': return cc_type_basic(TY_DOUBLE);
	}
	return cc_type_basic(TY_VOID);
}

VPRIV const struct cxx_builtin_function *cxx_builtin_function_find(const char *name, size_t len, uint32_t nargs, size_t *index)
{
	for (size_t i = 0; i < sizeof cxx_builtin_functions / sizeof *cxx_builtin_functions; i++)
		if (strlen(cxx_builtin_functions[i].name) == len && !memcmp(cxx_builtin_functions[i].name, name, len) &&
		    strlen(cxx_builtin_functions[i].params) == nargs) {
			*index = i;
			return &cxx_builtin_functions[i];
		}
	return NULL;
}

/* __builtin_isnan, isinf, isfinite, isnormal: 1 2 3 4 (0: not one) */
VPRIV int cxx_float_class_builtin(const char *name, size_t len)
{
	if (len == 15 && !memcmp(name, "__builtin_isnan", 15)) return 1;
	if (len == 15 && !memcmp(name, "__builtin_isinf", 15)) return 2;
	if (len == 18 && !memcmp(name, "__builtin_isfinite", 18)) return 3;
	if (len == 18 && !memcmp(name, "__builtin_isnormal", 18)) return 4;
	return 0;
}

/* a builtin call's type, for the builtins vcc knows as functions or simple operations (arg0: the first argument's type) */
VPRIV bool cxx_builtin_function_type(cxx *c, const char *name, size_t len, uint32_t nargs, qty arg0, qty *ret)
{
	const struct cxx_builtin_function *b;
	size_t i;

	if ((b = cxx_builtin_function_find(name, len, nargs, &i))) {
		*ret = cxx_builtin_code_type(c, b->ret);
		return true;
	}
	if (len == 31 && !memcmp(name, "__builtin_is_constant_evaluated", 31) && nargs == 0) {
		*ret = cc_type_basic(TY_BOOL);
		return true;
	}
	if (len == 16 && !memcmp(name, "__builtin_expect", 16) && nargs == 2 && arg0 != CXX_TY_UNKNOWN) {
		*ret = cc_type_basic(TY_LONG);
		return true;
	}
	if (len == 20 && !memcmp(name, "__builtin_constant_p", 20) && nargs == 1) {
		*ret = cc_type_basic(TY_INT);
		return true;
	}
	if (nargs == 1 && arg0 != CXX_TY_UNKNOWN && cxx_float_class_builtin(name, len) &&
	    (AST_TYPE_KIND(c->ast, arg0) == TY_FLOAT || AST_TYPE_KIND(c->ast, arg0) == TY_DOUBLE || AST_TYPE_KIND(c->ast, arg0) == TY_LDOUBLE)) {
		*ret = cc_type_basic(TY_INT);
		return true;
	}
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19) && nargs == 1 && arg0 != CXX_TY_UNKNOWN) {
		*ret = cc_type_pointer(c->ast, arg0);
		return true;
	}
	if (len == 24 && !memcmp(name, "__builtin_assume_aligned", 24) && (nargs == 2 || nargs == 3)) {
		*ret = cc_type_pointer(c->ast, cc_type_basic(TY_VOID));
		return true;
	}
	return false;
}

/* ... and its code: a call of the library function, or the operation */
VPRIV void cxx_gen_builtin(cxx *c, const char *name, size_t len, const uint32_t *args, uint32_t nargs, uint32_t loc)
{
	cc *a = c->ast;
	const struct cxx_builtin_function *b;
	size_t i;

	if (!cxx_gen_on(c))
		return;
	if (len == 31 && !memcmp(name, "__builtin_is_constant_evaluated", 31)) {
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(TY_BOOL), loc);	/* (code that runs: not a constant evaluation) */
		return;
	}
	if (len == 16 && !memcmp(name, "__builtin_expect", 16)) {
		if (args[0])
			c->enode = cc_conversion_apply(a, cc_conversion_decay(a, args[0]), cc_type_basic(TY_LONG));
		return;
	}
	if (len == 20 && !memcmp(name, "__builtin_constant_p", 20)) {
		c->enode = cc_node_integer_new(a, 0, cc_type_basic(TY_INT), loc);	/* (not folded here: never a constant) */
		return;
	}
	if (cxx_float_class_builtin(name, len) && nargs == 1 && args[0]) {
		/* by comparisons: NaN is not equal to itself; x - x is NaN for an infinity or a NaN, else 0 */
		int k = cxx_float_class_builtin(name, len);
		qty ft;
		uint32_t (*x)(cc *, uint32_t) = cc_node_rvalue;
		uint32_t v, r;

		v = x(a, args[0]);
		ft = QUNQ(AST_NODE_AT(a, v)->ty);
		if (AST_TYPE_KIND(a, ft) != TY_FLOAT && AST_TYPE_KIND(a, ft) != TY_DOUBLE && AST_TYPE_KIND(a, ft) != TY_LDOUBLE)
			return;
		if (k == 4 && AST_TYPE_KIND(a, ft) == TY_LDOUBLE)
			return;	/* (its smallest normal: not a double) */
#define ARG() x(a, args[0])
#define DIFF() cc_binary_build(a, LEX_TOKEN_MINUS, ARG(), ARG(), loc)
		if (k == 1)
			r = cc_binary_build(a, LEX_TOKEN_EXCLAMATION_EQUAL, ARG(), ARG(), loc);
		else if (k == 2)
			r = cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, ARG(), ARG(), loc),
					    cc_binary_build(a, LEX_TOKEN_EXCLAMATION_EQUAL, DIFF(), DIFF(), loc), loc);
		else {
			r = cc_binary_build(a, LEX_TOKEN_EQUAL_EQUAL, DIFF(), DIFF(), loc);
			if (k == 4) {
				/* finite and |x| >= the smallest normal */
				double mn = AST_TYPE_KIND(a, ft) == TY_FLOAT ? 0x1p-126 : 0x1p-1022;
				uint32_t lo = cc_node_float_new(a, mn, ft, loc), nlo = cc_node_float_new(a, -mn, ft, loc);

				r = cc_binary_build(a, LEX_TOKEN_AMPERSAND_AMPERSAND, r,
						    cc_binary_build(a, LEX_TOKEN_PIPE_PIPE, cc_binary_build(a, LEX_TOKEN_GREATER_EQUAL, ARG(), lo, loc),
								    cc_binary_build(a, LEX_TOKEN_LESS_EQUAL, ARG(), nlo, loc), loc), loc);
			}
		}
#undef ARG
#undef DIFF
		c->enode = cc_conversion_apply(a, r, cc_type_basic(TY_INT));
		return;
	}
	if (len == 19 && !memcmp(name, "__builtin_addressof", 19)) {
		if (args[0] && (AST_NODE_AT(a, args[0])->flags & NF_LVALUE))
			c->enode = cc_address_of_build(a, args[0], loc);
		return;
	}
	if (len == 24 && !memcmp(name, "__builtin_assume_aligned", 24)) {
		/* the pointer itself, as void * (the alignment it promises: not used at -O0) */
		uint32_t p = args[0] ? cc_conversion_decay(a, args[0]) : 0;

		if (p && AST_TYPE_KIND(a, AST_NODE_AT(a, p)->ty) == TY_PTR)
			c->enode = cc_cast_insert(a, p, cc_type_pointer(a, cc_type_basic(TY_VOID)), CK_NOP);
		return;
	}
	if (!(b = cxx_builtin_function_find(name, len, nargs, &i)) || i >= 128)
		return;
	if (!c->gen_builtin_syms[i]) {
		qty ps[4];
		uint32_t s;

		for (uint32_t k = 0; k < nargs; k++)
			ps[k] = cxx_builtin_code_type(c, b->params[k]);
		s = cc_symbol_new(a, S_FUNC, vintern_cstr(b->symbol), cxx_type_function(c, cxx_builtin_code_type(c, b->ret), ps, nargs, false), loc);
		a->syms[s].flags |= SF_GLOBAL;
		a->syms[s].depth = 0;
		c->gen_builtin_syms[i] = s;
	}
	{
		cc_id_list list;
		uint32_t s = c->gen_builtin_syms[i], fn, n;

		cc_id_list_init(&list);
		for (uint32_t k = 0; k < nargs; k++) {
			if (!args[k])
				return;
			cc_id_list_append(a, &list, cc_conversion_apply(a, cc_conversion_decay(a, args[k]), cxx_builtin_code_type(c, b->params[k])));
		}
		fn = cc_node_rvalue(a, cxx_gen_var_node(c, s, loc));
		n = cc_node_new(a, N_CALL, cxx_builtin_code_type(c, b->ret), loc);
		AST_NODE_AT(a, n)->a = fn;
		AST_NODE_AT(a, n)->b = cc_id_list_freeze(a, &list);
		AST_NODE_AT(a, n)->c = nargs;
		c->enode = n;
	}
}

/* ---- constructors and destructors ------------------------------------------------------------ */

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc);

VPRIV uint32_t cxx_gen_var_node(cxx *c, uint32_t s, uint32_t loc)
{
	uint32_t n = cc_node_new(c->ast, N_VAR, c->ast->syms[s].ty, loc);

	AST_NODE_AT(c->ast, n)->a = s;
	AST_NODE_AT(c->ast, n)->flags = NF_LVALUE;
	return n;
}
