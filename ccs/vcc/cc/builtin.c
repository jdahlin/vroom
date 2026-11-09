/* vcc front end: builtins: the table and the special forms */
#include "ccimpl.h"

static const builtin builtins[] = {
	{ "__builtin_constant_p", BI_CONSTANT_P, 1, "i." },
	{ "__builtin_expect", BI_EXPECT, 1, "lll" },
	{ "__builtin_expect_with_probability", BI_EXPECT_PROB, 1, "llld" },
	{ "__builtin_va_start", BI_VA_START, 0, "vV." },
	{ "__builtin_va_end", BI_VA_END, 0, "vV" },
	{ "__builtin_va_copy", BI_VA_COPY, 0, "vVV" },
	{ "__builtin_clz", BI_CLZ, 1, "iu" }, { "__builtin_clzl", BI_CLZL, 1, "iL" }, { "__builtin_clzll", BI_CLZLL, 1, "iW" },
	{ "__builtin_ctz", BI_CTZ, 1, "iu" }, { "__builtin_ctzl", BI_CTZL, 1, "iL" }, { "__builtin_ctzll", BI_CTZLL, 1, "iW" },
	{ "__builtin_ffs", BI_FFS, 1, "ii" }, { "__builtin_ffsl", BI_FFSL, 1, "il" }, { "__builtin_ffsll", BI_FFSLL, 1, "iw" },
	{ "__builtin_popcount", BI_POPCOUNT, 1, "iu" }, { "__builtin_popcountl", BI_POPCOUNTL, 1, "iL" },
	{ "__builtin_popcountll", BI_POPCOUNTLL, 1, "iW" },
	{ "__builtin_clzg", BI_CLZG, 1, "i." }, { "__builtin_ctzg", BI_CTZG, 1, "i." },
	{ "__builtin_popcountg", BI_POPCOUNTG, 1, "i." },
	{ "__builtin_parity", BI_PARITY, 1, "iu" }, { "__builtin_parityl", BI_PARITYL, 1, "iL" },
	{ "__builtin_parityll", BI_PARITYLL, 1, "iW" },
	{ "__builtin_clrsb", BI_CLRSB, 1, "ii" }, { "__builtin_clrsbl", BI_CLRSBL, 1, "il" },
	{ "__builtin_clrsbll", BI_CLRSBLL, 1, "iw" },
	{ "__builtin_bswap16", BI_BSWAP16, 1, "hh" }, { "__builtin_bswap32", BI_BSWAP32, 1, "uu" },
	{ "__builtin_bswap64", BI_BSWAP64, 1, "WW" },
	{ "__builtin_bitreverse8", BI_BITREVERSE8, 1, "CC" }, { "__builtin_bitreverse16", BI_BITREVERSE16, 1, "hh" },
	{ "__builtin_bitreverse32", BI_BITREVERSE32, 1, "uu" }, { "__builtin_bitreverse64", BI_BITREVERSE64, 1, "WW" },
	{ "__builtin_unreachable", BI_UNREACHABLE, 0, "v" },
	{ "__builtin_trap", BI_TRAP, 0, "v" }, { "__builtin_debugtrap", BI_DEBUGTRAP, 0, "v" },
	{ "__builtin_return_address", BI_RETURN_ADDRESS, 0, "pu" },
	{ "__builtin_frame_address", BI_FRAME_ADDRESS, 0, "pu" },
	{ "__builtin_extract_return_addr", BI_EXTRACT_RETURN_ADDR, 0, "pp" },
	{ "__builtin_add_overflow", BI_ADD_OVERFLOW, 0, "b." },
	{ "__builtin_sub_overflow", BI_SUB_OVERFLOW, 0, "b." },
	{ "__builtin_mul_overflow", BI_MUL_OVERFLOW, 0, "b." },
	{ "__builtin_sadd_overflow", BI_ADD_OVERFLOW, 0, "bii." },
	{ "__builtin_saddl_overflow", BI_ADD_OVERFLOW, 0, "bll." },
	{ "__builtin_saddll_overflow", BI_ADD_OVERFLOW, 0, "bww." },
	{ "__builtin_uadd_overflow", BI_ADD_OVERFLOW, 0, "buu." },
	{ "__builtin_uaddl_overflow", BI_ADD_OVERFLOW, 0, "bLL." },
	{ "__builtin_uaddll_overflow", BI_ADD_OVERFLOW, 0, "bWW." },
	{ "__builtin_ssub_overflow", BI_SUB_OVERFLOW, 0, "bii." },
	{ "__builtin_ssubl_overflow", BI_SUB_OVERFLOW, 0, "bll." },
	{ "__builtin_ssubll_overflow", BI_SUB_OVERFLOW, 0, "bww." },
	{ "__builtin_usub_overflow", BI_SUB_OVERFLOW, 0, "buu." },
	{ "__builtin_usubl_overflow", BI_SUB_OVERFLOW, 0, "bLL." },
	{ "__builtin_usubll_overflow", BI_SUB_OVERFLOW, 0, "bWW." },
	{ "__builtin_smul_overflow", BI_MUL_OVERFLOW, 0, "bii." },
	{ "__builtin_smull_overflow", BI_MUL_OVERFLOW, 0, "bll." },
	{ "__builtin_smulll_overflow", BI_MUL_OVERFLOW, 0, "bww." },
	{ "__builtin_umul_overflow", BI_MUL_OVERFLOW, 0, "buu." },
	{ "__builtin_umull_overflow", BI_MUL_OVERFLOW, 0, "bLL." },
	{ "__builtin_umulll_overflow", BI_MUL_OVERFLOW, 0, "bWW." },
	{ "__builtin_addcb", BI_ADDC, 0, "CCCC." },
	{ "__builtin_addcs", BI_ADDC, 0, "hhhh." },
	{ "__builtin_addc", BI_ADDC, 0, "uuuu." },
	{ "__builtin_addcl", BI_ADDC, 0, "LLLL." },
	{ "__builtin_addcll", BI_ADDC, 0, "WWWW." },
	{ "__builtin_subcb", BI_SUBC, 0, "CCCC." },
	{ "__builtin_subcs", BI_SUBC, 0, "hhhh." },
	{ "__builtin_subc", BI_SUBC, 0, "uuuu." },
	{ "__builtin_subcl", BI_SUBC, 0, "LLLL." },
	{ "__builtin_subcll", BI_SUBC, 0, "WWWW." },
	{ "__builtin_object_size", BI_OBJECT_SIZE, 0, "zPi" },
	{ "__builtin_dynamic_object_size", BI_DYNAMIC_OBJECT_SIZE, 0, "zPi" },
	{ "__builtin_counted_by_ref", BI_COUNTED_BY_REF, 0, "p." },
	{ "__builtin_alloca", BI_ALLOCA, 0, "pz" },
	{ "__builtin_alloca_uninitialized", BI_ALLOCA_UNINIT, 0, "pz" },
	{ "__builtin_prefetch", BI_PREFETCH, 0, "vP." },
	{ "__builtin_assume_aligned", BI_ASSUME_ALIGNED, 0, "pPz." },
	{ "__builtin_assume", BI_ASSUME, 0, "vb" },
	{ "__builtin_classify_type", BI_CLASSIFY_TYPE, 1, "i." },
	{ "__builtin_is_constant_evaluated", BI_IS_CONSTANT_EVALUATED, 1, "b" },
	{ "__builtin_strlen", BI_STRLEN, 1, "zS" },
	{ "__builtin_huge_val", BI_HUGE_VAL, 1, "d" }, { "__builtin_huge_valf", BI_HUGE_VALF, 1, "f" },
	{ "__builtin_huge_vall", BI_HUGE_VAL, 1, "D" },
	{ "__builtin_inf", BI_INF, 1, "d" }, { "__builtin_inff", BI_INFF, 1, "f" }, { "__builtin_infl", BI_INF, 1, "D" },
	{ "__builtin_nan", BI_NAN, 1, "dS" }, { "__builtin_nanf", BI_NANF, 1, "fS" }, { "__builtin_nanl", BI_NAN, 1, "DS" },
	{ "__builtin_isnan", BI_ISNAN, 1, "i." }, { "__builtin_isinf", BI_ISINF, 1, "i." },
	{ "__builtin_isfinite", BI_ISFINITE, 1, "i." }, { "__builtin_signbit", BI_SIGNBIT, 1, "i." },
	{ "__builtin_fpclassify", BI_FPCLASSIFY, 1, "i." }, { "__builtin_isinf_sign", BI_ISINF_SIGN, 1, "i." },
	{ "__builtin_isnormal", BI_ISNORMAL, 1, "i." },
	{ "__builtin_isgreater", BI_ISGREATER, 1, "i." }, { "__builtin_isgreaterequal", BI_ISGREATEREQUAL, 1, "i." },
	{ "__builtin_isless", BI_ISLESS, 1, "i." }, { "__builtin_islessequal", BI_ISLESSEQUAL, 1, "i." },
	{ "__builtin_islessgreater", BI_ISLESSGREATER, 1, "i." }, { "__builtin_isunordered", BI_ISUNORDERED, 1, "i." },
	{ "__builtin_arm_rsr64", BI_ARM_RSR, 0, "WS" },
	{ "__atomic_load_n", BI_ATOMIC_LOAD_N, 0, "v." }, { "__atomic_store_n", BI_ATOMIC_STORE_N, 0, "v." },
	{ "__atomic_exchange_n", BI_ATOMIC_EXCHANGE_N, 0, "v." },
	{ "__atomic_compare_exchange_n", BI_ATOMIC_CMPXCHG_N, 0, "v." },
	{ "__atomic_load", BI_ATOMIC_LOAD, 0, "v." }, { "__atomic_store", BI_ATOMIC_STORE, 0, "v." },
	{ "__atomic_exchange", BI_ATOMIC_EXCHANGE, 0, "v." },
	{ "__atomic_compare_exchange", BI_ATOMIC_CMPXCHG, 0, "v." },
	{ "__atomic_fetch_add", BI_ATOMIC_FETCH_ADD, 0, "v." }, { "__atomic_fetch_sub", BI_ATOMIC_FETCH_SUB, 0, "v." },
	{ "__atomic_fetch_and", BI_ATOMIC_FETCH_AND, 0, "v." }, { "__atomic_fetch_or", BI_ATOMIC_FETCH_OR, 0, "v." },
	{ "__atomic_fetch_xor", BI_ATOMIC_FETCH_XOR, 0, "v." },
	{ "__atomic_add_fetch", BI_ATOMIC_ADD_FETCH, 0, "v." }, { "__atomic_sub_fetch", BI_ATOMIC_SUB_FETCH, 0, "v." },
	{ "__atomic_and_fetch", BI_ATOMIC_AND_FETCH, 0, "v." }, { "__atomic_or_fetch", BI_ATOMIC_OR_FETCH, 0, "v." },
	{ "__atomic_xor_fetch", BI_ATOMIC_XOR_FETCH, 0, "v." },
	/* legacy __sync_*: the __atomic_* forms with seq_cst (sig "s.": atomic_call appends the order) */
	{ "__sync_fetch_and_add", BI_ATOMIC_FETCH_ADD, 0, "s." }, { "__sync_fetch_and_sub", BI_ATOMIC_FETCH_SUB, 0, "s." },
	{ "__sync_fetch_and_and", BI_ATOMIC_FETCH_AND, 0, "s." }, { "__sync_fetch_and_or", BI_ATOMIC_FETCH_OR, 0, "s." },
	{ "__sync_fetch_and_xor", BI_ATOMIC_FETCH_XOR, 0, "s." },
	{ "__sync_add_and_fetch", BI_ATOMIC_ADD_FETCH, 0, "s." }, { "__sync_sub_and_fetch", BI_ATOMIC_SUB_FETCH, 0, "s." },
	{ "__sync_and_and_fetch", BI_ATOMIC_AND_FETCH, 0, "s." }, { "__sync_or_and_fetch", BI_ATOMIC_OR_FETCH, 0, "s." },
	{ "__sync_xor_and_fetch", BI_ATOMIC_XOR_FETCH, 0, "s." },
	{ "__sync_lock_test_and_set", BI_ATOMIC_EXCHANGE_N, 0, "s." },
	{ "__sync_val_compare_and_swap", BI_SYNC_VAL_CAS, 0, "s." },
	{ "__sync_bool_compare_and_swap", BI_SYNC_BOOL_CAS, 0, "s." },
	{ "__sync_lock_release", BI_SYNC_LOCK_RELEASE, 0, "s." },
	{ "__sync_synchronize", BI_SYNC_SYNCHRONIZE, 0, "s." },
	{ "__atomic_thread_fence", BI_ATOMIC_THREAD_FENCE, 0, "vi" },
	{ "__atomic_signal_fence", BI_ATOMIC_SIGNAL_FENCE, 0, "vi" },
	{ "__atomic_always_lock_free", BI_ATOMIC_LOCK_FREE, 1, "bzP" },
	{ "__atomic_is_lock_free", BI_ATOMIC_LOCK_FREE, 1, "bzP" },
	/* library functions */
	{ "__builtin_memcpy", BI_LIB, 0, "ppPz" }, { "__builtin_memmove", BI_LIB, 0, "ppPz" },
	{ "__builtin_memset", BI_LIB, 0, "ppiz" }, { "__builtin_memcmp", BI_LIB, 0, "iPPz" },
	{ "__builtin_memchr", BI_LIB, 0, "pPiz" }, { "__builtin_bzero", BI_LIB, 0, "vpz" },
	{ "__builtin_strcmp", BI_LIB, 0, "iSS" }, { "__builtin_strncmp", BI_LIB, 0, "iSSz" },
	{ "__builtin_strcpy", BI_LIB, 0, "ssS" }, { "__builtin_strncpy", BI_LIB, 0, "ssSz" },
	{ "__builtin_strcat", BI_LIB, 0, "ssS" }, { "__builtin_strncat", BI_LIB, 0, "ssSz" },
	{ "__builtin_strchr", BI_LIB, 0, "sSi" }, { "__builtin_strrchr", BI_LIB, 0, "sSi" },
	{ "__builtin_strstr", BI_LIB, 0, "sSS" }, { "__builtin_strnlen", BI_LIB, 0, "zSz" },
	{ "__builtin_abort", BI_LIB, 0, "v" }, { "__builtin_exit", BI_LIB, 0, "vi" },
	{ "__builtin_malloc", BI_LIB, 0, "pz" }, { "__builtin_free", BI_LIB, 0, "vp" },
	{ "__builtin_calloc", BI_LIB, 0, "pzz" }, { "__builtin_realloc", BI_LIB, 0, "ppz" },
	{ "__builtin_printf", BI_LIB, 0, "iS." }, { "__builtin_sprintf", BI_LIB, 0, "isS." },
	{ "__builtin_snprintf", BI_LIB, 0, "iszS." }, { "__builtin_vsnprintf", BI_LIB, 0, "iszSV" },
	{ "__builtin_vsprintf", BI_LIB, 0, "isSV" }, { "__builtin_puts", BI_LIB, 0, "iS" },
	{ "__builtin_abs", BI_LIB, 1, "ii" }, { "__builtin_labs", BI_LIB, 1, "ll" }, { "__builtin_llabs", BI_LIB, 1, "ww" },
	{ "__builtin_conj", BI_CONJ, 0, "d." }, { "__builtin_conjf", BI_CONJ, 0, "f." }, { "__builtin_conjl", BI_CONJ, 0, "D." },
	{ "__builtin_creal", BI_CREAL, 0, "d." }, { "__builtin_crealf", BI_CREAL, 0, "f." }, { "__builtin_creall", BI_CREAL, 0, "D." },
	{ "__builtin_cimag", BI_CIMAG, 0, "d." }, { "__builtin_cimagf", BI_CIMAG, 0, "f." }, { "__builtin_cimagl", BI_CIMAG, 0, "D." },
	{ "__builtin_fabs", BI_LIB, 1, "dd" }, { "__builtin_fabsf", BI_LIB, 1, "ff" }, { "__builtin_fabsl", BI_LIB, 1, "DD" },
	{ "__builtin_copysign", BI_LIB, 1, "ddd" }, { "__builtin_copysignf", BI_LIB, 1, "fff" },
	{ "__builtin_copysignl", BI_LIB, 1, "DDD" },
	{ "__builtin_sqrt", BI_LIB, 0, "dd" }, { "__builtin_sqrtf", BI_LIB, 0, "ff" },
	{ "__builtin_floor", BI_LIB, 0, "dd" }, { "__builtin_ceil", BI_LIB, 0, "dd" },
	{ "__builtin_fmax", BI_LIB, 0, "ddd" }, { "__builtin_fmin", BI_LIB, 0, "ddd" },
	{ "__builtin_ldexp", BI_LIB, 0, "ddi" }, { "__builtin_ldexpf", BI_LIB, 0, "ffi" },
	{ "__builtin_ldexpl", BI_LIB, 0, "DDi" }, { "__builtin_scalbn", BI_LIB, 0, "ddi" },
	{ "__builtin_floorf", BI_LIB, 0, "ff" }, { "__builtin_ceilf", BI_LIB, 0, "ff" },
	{ "__builtin_trunc", BI_LIB, 0, "dd" }, { "__builtin_truncf", BI_LIB, 0, "ff" },
	{ "__builtin_round", BI_LIB, 0, "dd" }, { "__builtin_roundf", BI_LIB, 0, "ff" },
	{ "__builtin_fmaxf", BI_LIB, 0, "fff" }, { "__builtin_fminf", BI_LIB, 0, "fff" },
	{ "__builtin___memcpy_chk", BI_LIB, 0, "ppPzz" }, { "__builtin___memmove_chk", BI_LIB, 0, "ppPzz" },
	{ "__builtin___memset_chk", BI_LIB, 0, "ppizz" }, { "__builtin___strcpy_chk", BI_LIB, 0, "ssSz" },
	{ "__builtin___stpcpy_chk", BI_LIB, 0, "ssSz" }, { "__builtin___strcat_chk", BI_LIB, 0, "ssSz" },
	{ "__builtin___strncpy_chk", BI_LIB, 0, "ssSzz" }, { "__builtin___strncat_chk", BI_LIB, 0, "ssSzz" },
	{ "__builtin___stpncpy_chk", BI_LIB, 0, "ssSzz" }, { "__builtin___strlcpy_chk", BI_LIB, 0, "zsSzz" },
	{ "__builtin___strlcat_chk", BI_LIB, 0, "zsSzz" }, { "__builtin___memccpy_chk", BI_LIB, 0, "ppPizz" },
	{ "__builtin___sprintf_chk", BI_LIB, 0, "isizS." }, { "__builtin___snprintf_chk", BI_LIB, 0, "iszizS." },
	{ "__builtin___vsprintf_chk", BI_LIB, 0, "isizSV" }, { "__builtin___vsnprintf_chk", BI_LIB, 0, "iszizSV" },
	{ NULL, 0, 0, NULL }
};

/* name of a library builtin (sym val of an S_BUILTIN) */
const char *cc_builtin_name(int64_t i) { return builtins[i].name; }

static qty sigtype(cc *c, char ch)
{
	switch (ch) {
	case 'v': return prim(TY_VOID);
	case 'b': return prim(TY_BOOL);
	case 'C': return prim(TY_UCHAR);
	case 'h': return prim(TY_USHORT);
	case 'i': return prim(TY_INT);
	case 'u': return prim(TY_UINT);
	case 'l': return prim(TY_LONG);
	case 'L': return prim(TY_ULONG);
	case 'w': return prim(TY_LLONG);
	case 'W': return prim(TY_ULLONG);
	case 'z': return c->t_size;
	case 'd': return prim(TY_DOUBLE);
	case 'f': return prim(TY_FLOAT);
	case 'D': return prim(TY_LDOUBLE);
	case 'p': return ty_ptr(c, prim(TY_VOID));
	case 'P': return ty_ptr(c, QT(TY_VOID, Q_CONST));
	case 's': return ty_ptr(c, prim(TY_CHAR));
	case 'S': return ty_ptr(c, QT(TY_CHAR, Q_CONST));
	case 'V': return c->t_valist;
	default: abort();
	}
}

/* the function type of a builtin's signature */
static qty builtin_type(cc *c, const builtin *b)
{
	qty par[16];
	uint32_t n = 0;
	int fl = 0;

	for (const char *s = b->sig + 1; *s; s++) {
		if (*s == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		par[n++] = sigtype(c, *s);
	}
	return ty_func(c, sigtype(c, b->sig[0]), par, n, fl);
}

/* ---------------- expressions: builtins ---------------- */

static uint32_t *bi_dir[NDIR];

VPRIV void init_builtins(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	/* the table is shared by all TUs; building it twice is harmless */
	for (uint32_t i = 0; builtins[i].name; i++) {
		size_t l = strlen(builtins[i].name);
		char *s = malloc(l + 17);
		uint32_t id;

		memcpy(s, builtins[i].name, l);
		memset(s + l, 0, 17);
		id = vintern_copy(s, l);
		if (!bi_dir[id >> DIR_BITS]) {
			uint32_t *pg = calloc(DIR_PAGE, sizeof(uint32_t));

			bi_dir[id >> DIR_BITS] = pg;
		}
		bi_dir[id >> DIR_BITS][id & (DIR_PAGE - 1)] = i + 1;
		free(s);
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

VPRIV const builtin *builtin_of(uint32_t id)
{
	uint32_t *pg = bi_dir[id >> DIR_BITS];
	uint32_t i = pg ? pg[id & (DIR_PAGE - 1)] : 0;

	return i ? &builtins[i - 1] : NULL;
}

static int classify_type(cc *c, qty t)
{
	switch (KIND(c, t)) {
	case TY_VOID: return 0;
	case TY_BOOL: return 4;
	case TY_ENUM: return 3;
	case TY_PTR: return 5;
	case TY_STRUCT: return 12;
	case TY_UNION: return 13;
	case TY_ARRAY: return 5;
	case TY_FUNC: return 5;
	default:
		if (is_float(c, t))
			return 8;
		return 1;
	}
}

bool cc_eval(cc *c, uint32_t n, cval *v) { return eval(c, n, v); }
bool cc_is_signed(cc *c, qty t) { return is_signed(c, t); }

VPRIV bool eval_builtin(cc *c, const node *x, cval *v)
{
	const uint32_t *arg = &c->lists[x->b];
	cval a;

	v->kind = CV_INT;
	v->ice = true;
	switch (x->aux) {
	case BI_CONSTANT_P:
		if (eval(c, arg[0], &a) && (a.kind == CV_INT || a.kind == CV_FLT || (a.kind == CV_ADDR && a.str && a.i == 0)))
			v->i = 1;
		else
			v->i = 0;
		return true;
	case BI_EXPECT:
	case BI_EXPECT_PROB:
		if (!eval(c, arg[0], v))
			return false;
		v->ice = v->ice && v->kind == CV_INT;
		return true;
	case BI_CLASSIFY_TYPE:
		v->i = classify_type(c, N(arg[0])->ty);
		return true;
	case BI_STRLEN: {
		uint32_t s = arg[0];

		while (N(s)->op == N_CAST)
			s = N(s)->a;
		if (N(s)->op != N_STR)
			return false;
		v->i = (i128)strlen(c->strs[N(s)->a].s);
		v->ice = false;
		return true;
	}
	case BI_HUGE_VAL: case BI_HUGE_VALF: case BI_INF: case BI_INFF:
		v->kind = CV_FLT, v->f = __builtin_inf(), v->ice = false;
		return true;
	case BI_NAN: case BI_NANF:
		v->kind = CV_FLT, v->f = __builtin_nan(""), v->ice = false;
		return true;
	case BI_CLZ: case BI_CLZL: case BI_CLZLL: case BI_CTZ: case BI_CTZL: case BI_CTZLL:
	case BI_FFS: case BI_FFSL: case BI_FFSLL: case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
	case BI_PARITY: case BI_PARITYL: case BI_PARITYLL: case BI_BSWAP16: case BI_BSWAP32: case BI_BSWAP64:
	case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
		qty at;
		unsigned bits;
		u128 u;

		if (!eval(c, arg[0], &a) || a.kind != CV_INT)
			return false;
		at = N(arg[0])->ty;
		bits = (unsigned)ty_size(c, at) * 8;
		u = (u128)trunc_to(c, a.i, unsigned_of(c, at));
		v->ice = a.ice;
		switch (x->aux) {
		case BI_CLZ: case BI_CLZL: case BI_CLZLL:
			if (!u)
				return false;
			v->i = clz128(u, bits);
			break;
		case BI_CTZ: case BI_CTZL: case BI_CTZLL:
			if (!u)
				return false;
			v->i = __builtin_ctzll((uint64_t)u);
			break;
		case BI_FFS: case BI_FFSL: case BI_FFSLL:
			v->i = u ? __builtin_ctzll((uint64_t)u) + 1 : 0;
			break;
		case BI_POPCOUNT: case BI_POPCOUNTL: case BI_POPCOUNTLL:
			v->i = __builtin_popcountll((uint64_t)u);
			break;
		case BI_PARITY: case BI_PARITYL: case BI_PARITYLL:
			v->i = __builtin_popcountll((uint64_t)u) & 1;
			break;
		case BI_CLRSB: case BI_CLRSBL: case BI_CLRSBLL: {
			u128 s = (u >> (bits - 1)) & 1 ? ~u & (((u128)1 << bits) - 1) : u;

			v->i = clz128(s, bits) - 1;
			break;
		}
		case BI_BSWAP16: v->i = __builtin_bswap16((uint16_t)u); break;
		case BI_BSWAP32: v->i = __builtin_bswap32((uint32_t)u); break;
		case BI_BSWAP64: v->i = (i128)__builtin_bswap64((uint64_t)u); break;
		}
		v->i = trunc_to(c, v->i, x->ty);
		return true;
	}
	default:
		return false;
	}
}

VPRIV uint32_t args_list(cc *c, vec *args)
{
	vinit(args);
	expect(c, T_LPAREN, "'('");
	if (!is(c, T_RPAREN)) {
		do
			vpush(c, args, assign_expr(c));
		while (accept(c, T_COMMA));
	}
	expect(c, T_RPAREN, "')'");
	return args->n;
}

/* t without qualifiers, down through array element types (clang's unqualified array type) */
static qty unqual_array(cc *c, qty t)
{
	const ctype *x;

	t = QUNQ(t);
	if (!is_array(c, t))
		return t;
	x = T(c, t);
	if (x->flags & TF_VLA)
		return t;
	{
		qty e = x->base, ue = unqual_array(c, e);
		int fl = x->flags & TF_INCOMPLETE;

		return ue == e ? t : ty_array(c, ue, fl ? 0 : arr_len(c, t), fl);
	}
}

VPRIV uint32_t mk_builtin(cc *c, int id, qty ty, uint32_t loc, const vec *args)
{
	uint32_t n = mk(c, N_BUILTIN, ty, loc);

	N(n)->aux = (uint16_t)id;
	N(n)->b = vfreeze(c, args);
	N(n)->c = args->n;
	return n;
}

VPRIV uint32_t offsetof_expr(cc *c, uint32_t loc)
{
	qty t;
	uint32_t res;
	i128 off = 0;
	uint32_t dyn = 0;	/* non-constant part (node), 0 if none */

	expect(c, T_LPAREN, "'('");
	t = type_name(c);
	expect(c, T_COMMA, "','");
	for (bool first = true;; first = false) {
		if (first || accept(c, T_PERIOD)) {
			uint32_t path[16], ti;
			int d;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			if (!is_record(c, t)) {
				cc_error(c, tok(c)->loc, "offsetof requires struct, union, or class type");
				next(c);
				break;
			}
			ti = T(c, t)->aux;
			d = find_member(c, ti, tok(c)->id, path, 0);
			if (d < 0) {
				cc_error(c, tok(c)->loc, "no member named '%.*s'", IDF(tok(c)->id));
				next(c);
				break;
			}
			for (int i = 0; i < d; i++) {
				const field *f = &c->tags[ti].f[path[i]];

				off += (i128)(f->off / 8);
				t = f->ty;
				if (is_record(c, t))
					ti = T(c, t)->aux;
			}
			next(c);
		} else if (accept(c, T_LSQUARE)) {
			uint32_t ix = expr(c);
			i128 k;

			expect(c, T_RSQUARE, "']'");
			if (!is_array(c, t)) {
				cc_error(c, loc, "subscripted value is not an array");
				break;
			}
			t = T(c, t)->base;
			if (const_int(c, ix, &k)) {
				off += k * (i128)ty_size(c, t);
			} else {
				uint32_t sz = mk_int(c, (i128)ty_size(c, t), c->t_size, loc);
				uint32_t term = mk_bin(c, T_STAR, c->t_size, convert(c, rvalue(c, ix), c->t_size), sz, loc);

				dyn = dyn ? mk_bin(c, T_PLUS, c->t_size, dyn, term, loc) : term;
			}
		} else {
			break;
		}
	}
	expect(c, T_RPAREN, "')'");
	res = mk_int(c, off, c->t_size, loc);
	if (dyn)
		res = mk_bin(c, T_PLUS, c->t_size, res, dyn, loc);
	return res;
}

/*
 * GNU __atomic_* calls.  Argument layout (P: pointer to the atomic object
 * of type T, size 1/2/4/8/16; M: memory order, converted to int):
 *   load_n(P, M) -> T            store_n(P, T, M)          exchange_n(P, T, M) -> T
 *   compare_exchange_n(P, T *expected, T desired, bool weak, M, M) -> bool
 *   load(P, T *ret, M)           store(P, T *val, M)       exchange(P, T *val, T *ret, M)
 *   compare_exchange(P, T *expected, T *desired, bool weak, M, M) -> bool
 *   fetch_op / op_fetch(P, T, M) -> T (integers; pointers: bytes, as GCC)
 */
static uint32_t atomic_call(cc *c, const builtin *b, uint32_t loc)
{
	static const uint8_t nargs[] = {
		[BI_ATOMIC_LOAD_N - BI_ATOMIC_LOAD_N] = 2, [BI_ATOMIC_STORE_N - BI_ATOMIC_LOAD_N] = 3,
		[BI_ATOMIC_EXCHANGE_N - BI_ATOMIC_LOAD_N] = 3, [BI_ATOMIC_CMPXCHG_N - BI_ATOMIC_LOAD_N] = 6,
		[BI_ATOMIC_LOAD - BI_ATOMIC_LOAD_N] = 3, [BI_ATOMIC_STORE - BI_ATOMIC_LOAD_N] = 3,
		[BI_ATOMIC_EXCHANGE - BI_ATOMIC_LOAD_N] = 4, [BI_ATOMIC_CMPXCHG - BI_ATOMIC_LOAD_N] = 6,
	};
	int id = b->id, want = id <= BI_ATOMIC_CMPXCHG ? nargs[id - BI_ATOMIC_LOAD_N] :
			       id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS ? 4 : id == BI_SYNC_SYNCHRONIZE ? 1 : 3;
	vec args;
	qty pt, t, rt = prim(TY_VOID), tp;

	args_list(c, &args);
	for (uint32_t i = 0; i < args.n; i++)
		args.v[i] = decay(c, args.v[i]);
	if (id == BI_SYNC_SYNCHRONIZE) {	/* a full barrier: __atomic_thread_fence(seq_cst) */
		if (args.n)
			cc_error(c, loc, "too many arguments to function call");
		args.n = 0;
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
		return mk_builtin(c, BI_ATOMIC_THREAD_FENCE, prim(TY_VOID), loc, &args);
	}
	if (id == BI_SYNC_LOCK_RELEASE && args.n == 1) {	/* __atomic_store_n(p, 0, release) */
		vpush(c, &args, mk_int(c, 0, prim(TY_INT), loc));
		vpush(c, &args, mk_int(c, 3, prim(TY_INT), loc));
		id = BI_ATOMIC_STORE_N;
	}
	if (b->sig[0] == 's' && (int)args.n == want - 1)	/* __sync_*: sequentially consistent */
		vpush(c, &args, mk_int(c, 5, prim(TY_INT), loc));
	if ((int)args.n != want) {
		cc_error(c, loc, "too %s arguments to function call", (int)args.n < want ? "few" : "many");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	pt = N(args.v[0])->ty;
	if (!is_ptr(c, pt)) {
		cc_error(c, loc, "address argument to atomic builtin must be a pointer");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	t = QUNQ(T(c, pt)->base);
	tp = ty_ptr(c, t);
	if (!(T(c, t)->size == 1 || T(c, t)->size == 2 || T(c, t)->size == 4 || T(c, t)->size == 8 || T(c, t)->size == 16)) {
		cc_error(c, loc, "address argument to atomic builtin must be a pointer to a 1, 2, 4, 8 or 16 byte type");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	switch (id) {
	case BI_ATOMIC_LOAD_N:
		rt = t;
		break;
	case BI_ATOMIC_STORE_N:
		args.v[1] = convert(c, args.v[1], t);
		break;
	case BI_ATOMIC_EXCHANGE_N:
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	case BI_ATOMIC_CMPXCHG_N:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], t);
		args.v[3] = convert(c, args.v[3], prim(TY_BOOL));
		rt = prim(TY_BOOL);
		break;
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE:
		args.v[1] = convert(c, args.v[1], tp);
		break;
	case BI_ATOMIC_EXCHANGE:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], tp);
		break;
	case BI_ATOMIC_CMPXCHG:
		args.v[1] = convert(c, args.v[1], tp);
		args.v[2] = convert(c, args.v[2], tp);
		args.v[3] = convert(c, args.v[3], prim(TY_BOOL));
		rt = prim(TY_BOOL);
		break;
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS:	/* (p, old, new) -> previous *p, or whether it was old */
		if (!is_integer(c, t) && !is_ptr(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer");
		args.v[1] = convert(c, args.v[1], t);
		args.v[2] = convert(c, args.v[2], t);
		rt = id == BI_SYNC_VAL_CAS ? t : prim(TY_BOOL);
		break;
	default:	/* fetch_op / op_fetch */
		if (!is_integer(c, t) && !is_ptr(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer");
		args.v[1] = convert(c, args.v[1], is_ptr(c, t) ? c->t_ptrdiff : t);
		rt = t;
		break;
	}
	for (int i = want - (id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1); i < want; i++)
		args.v[i] = convert(c, args.v[i], prim(TY_INT));
	return mk_builtin(c, id, rt, loc, &args);
}

/* __builtin_clzg/ctzg/popcountg (x[, fallback]), clang's type-generic forms:
   x unsigned, up to 64 bits; the fixed-width builtin on x widened, clz
   corrected for the narrower width; the fallback is the result for x == 0
   (x is then read twice: one with side effects is not supported yet). */
static uint32_t generic_bitcount(cc *c, int id, uint32_t loc)
{
	vec args;
	uint32_t x, r, w;
	qty xt;
	unsigned bits;
	int fid;

	args_list(c, &args);
	if (args.n < 1 || args.n > (id == BI_POPCOUNTG ? 1u : 2u)) {
		cc_error(c, loc, "too %s arguments to function call", args.n ? "many" : "few");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	x = decay(c, args.v[0]);
	xt = QUNQ(N(x)->ty);	/* before promotion: the width counts */
	if (!is_integer(c, xt) || is_signed(c, xt) || KIND(c, xt) == TY_BOOL || ty_size(c, xt) > 8) {
		cc_error(c, N(x)->loc, "1st argument must be a scalar unsigned integer type of at most 64 bits");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	bits = (unsigned)ty_size(c, xt) * 8;
	w = convert(c, x, prim(bits == 64 ? TY_ULLONG : TY_UINT));
	fid = id == BI_CLZG ? (bits == 64 ? BI_CLZLL : BI_CLZ) : id == BI_CTZG ? (bits == 64 ? BI_CTZLL : BI_CTZ)
	    : (bits == 64 ? BI_POPCOUNTLL : BI_POPCOUNT);
	{
		vec a1;

		vinit(&a1);
		vpush(c, &a1, w);
		r = mk_builtin(c, fid, prim(TY_INT), loc, &a1);
	}
	if (id == BI_CLZG && bits < 32)
		r = binop(c, T_MINUS, r, mk_int(c, 32 - bits, prim(TY_INT), loc), loc);
	if (args.n == 2) {
		uint32_t fb = convert(c, rvalue(c, decay(c, args.v[1])), prim(TY_INT));

		if (has_side_effects(c, x)) {
			cc_error(c, N(x)->loc, "unsupported: a fallback with an argument that has side effects");
			return r;
		}
		r = conditional(c, binop(c, T_EXCLAIMEQ, x, mk_int(c, 0, xt, loc), loc), r, fb, loc);
	}
	return r;
}

VPRIV uint32_t builtin_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	qty rt = sigtype(c, b->sig[0]);

	switch (b->id) {
	case BI_CHOOSE: {
		uint32_t cond, x, y;
		i128 v = 0;

		expect(c, T_LPAREN, "'('");
		cond = assign_expr(c);
		expect(c, T_COMMA, "','");
		x = assign_expr(c);
		expect(c, T_COMMA, "','");
		y = assign_expr(c);
		expect(c, T_RPAREN, "')'");
		if (!const_int(c, decay(c, cond), &v))
			cc_error(c, N(cond)->loc, "'__builtin_choose_expr' requires a constant expression");
		return v ? x : y;
	}
	case BI_TYPES_COMPAT: {
		qty a, t;

		expect(c, T_LPAREN, "'('");
		a = type_name(c);
		expect(c, T_COMMA, "','");
		t = type_name(c);
		expect(c, T_RPAREN, "')'");
		return mk_int(c, compatible(c, unqual_array(c, a), unqual_array(c, t)), prim(TY_INT), loc);
	}
	case BI_OFFSETOF:
		return offsetof_expr(c, loc);
	case BI_VA_ARG: {
		uint32_t ap, n;
		qty t;

		expect(c, T_LPAREN, "'('");
		ap = assign_expr(c);
		expect(c, T_COMMA, "','");
		t = type_name(c);
		expect(c, T_RPAREN, "')'");
		n = mk(c, N_VAARG, t, loc);
		N(n)->a = ap;
		return n;
	}
	case BI_CONSTANT_P: {
		uint32_t n;

		c->unevaluated++;
		args_list(c, &args);
		c->unevaluated--;
		if (args.n != 1)
			cc_error(c, loc, "too %s arguments to function call", args.n ? "many" : "few");
		for (uint32_t i = 0; i < args.n; i++)
			args.v[i] = decay(c, args.v[i]);
		n = mk_builtin(c, b->id, prim(TY_INT), loc, &args);
		return n;
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH:
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS: case BI_SYNC_LOCK_RELEASE: case BI_SYNC_SYNCHRONIZE:
		return atomic_call(c, b, loc);
	case BI_CONJ: case BI_CREAL: case BI_CIMAG: {
		qty ct = ty_complex(c, sigtype(c, b->sig[0]));
		uint32_t a;

		args_list(c, &args);
		if (args.n != 1) {
			cc_error(c, loc, "too %s arguments to function call", args.n ? "many" : "few");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		a = convert(c, rvalue(c, args.v[0]), ct);
		if (b->id == BI_CONJ)
			return cplx_unary(c, T_TILDE, a, loc);
		return rvalue(c, cplx_part(c, a, b->id == BI_CIMAG));
	}
	case BI_CLZG: case BI_CTZG: case BI_POPCOUNTG:
		return generic_bitcount(c, b->id, loc);
	case BI_COUNTED_BY_REF: {
		uint32_t m;

		args_list(c, &args);
		m = args.n ? args.v[0] : 0;
		if (m && N(m)->op == N_MEMBER && node_field(c, N(m))->counted_by) {
			uint32_t cnt = member(c, N(m)->a, node_field(c, N(m))->counted_by, loc);

			return addr_of(c, cnt, loc);
		}
		return cast_node(c, mk_int(c, 0, prim(TY_INT), loc), ty_ptr(c, prim(TY_VOID)), CK_NULL);
	}
	default:
		break;
	}
	args_list(c, &args);
	{
		qty ft = builtin_type(c, b);
		const ctype *f = T(c, ft);
		uint32_t np = f->n;

		for (uint32_t i = 0; i < args.n; i++) {
			uint32_t a = decay(c, args.v[i]);

			if (i < np)
				a = convert(c, a, c->params[f->aux + i]);
			args.v[i] = a;
		}
		if (args.n < np || (args.n > np && !(f->flags & TF_VARIADIC)))
			cc_error(c, loc, "too %s arguments to function call", args.n < np ? "few" : "many");
		switch (b->id) {
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
			if (args.n != 3 || !is_ptr(c, N(args.v[2])->ty))
				cc_error(c, loc, "result argument to overflow builtin must be a pointer");
			break;
		case BI_ADDC: case BI_SUBC:
			if (args.n != 4 || !is_ptr(c, N(args.v[3])->ty))
				cc_error(c, loc, "carry-out argument to builtin must be a pointer");
			break;
		case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL:
		case BI_ISINF_SIGN: case BI_FPCLASSIFY:
			break;
		}
		if (b->id == BI_STRLEN) {
			/* folds when the string is known; otherwise the library call */
			uint32_t n = mk_builtin(c, b->id, rt, loc, &args);
			cval cv;

			if (cc_eval(c, n, &cv) && cv.kind == CV_INT)
				return n;
		}
		if (b->id == BI_LIB || b->id == BI_STRLEN) {
			/* a call to the library function the builtin names */
			uint32_t s = sym_new(c, S_BUILTIN, 0, ft, loc), fn, n;

			c->syms[s].val = b - builtins;
			fn = mk(c, N_VAR, ft, loc);
			N(fn)->a = s;
			n = mk(c, N_CALL, rt, loc);
			N(n)->a = decay(c, fn);
			N(n)->b = vfreeze(c, &args);
			N(n)->c = args.n;
			return n;
		}
		return mk_builtin(c, b->id, rt, loc, &args);
	}
}
