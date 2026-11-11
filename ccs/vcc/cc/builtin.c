/* vcc front end: builtins: the table and the special forms */
#include "ccimpl.h"

/* "too few/many arguments", as clang: the caret after the arguments, the callee's name underlined */
static void arity(cc *c, uint32_t loc, unsigned want, const vec *args)
{
	pp_span r = { loc, pp_tok_end(c->p, loc) };
	unsigned have = args->n;

	if (have && ISERR(args->v[have - 1]))	/* the ')' was missing (args_list): said */
		return;

	report(c, "error", c->prevloc, &r, 1, NULL, 0, "too %s arguments to function call, expected %u, have %u",
	       have < want ? "few" : "many", want, have);
}

static const builtin builtins[] = {
	{ "__builtin_elementwise_abs", BI_ELEMENTWISE_ABS, 1, "i." },
	{ "__builtin_ia32_movmskps", BI_X86_SIMD, 1, "iF" },
	{ "__builtin_ia32_shufps", BI_X86_SIMD, 1, "FFFi" },
	{ "__builtin_ia32_maxps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_minps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_cmpnleps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_psllwi128", BI_X86_SIMD, 1, "HHi" },
	{ "__builtin_ia32_psrlwi128", BI_X86_SIMD, 1, "HHi" },
	{ "__builtin_constant_p", BI_CONSTANT_P, 1, "i." },
	{ "__builtin_expect", BI_EXPECT, 1, "lll" },
	{ "__builtin_expect_with_probability", BI_EXPECT_PROB, 1, "llld" },
	{ "__builtin_va_start", BI_VA_START, 0, "vV." },
	{ "__builtin_c23_va_start", BI_VA_START, 0, "vV." },
	{ "__builtin_stdc_rotate_left", BI_STDC_ROTL, 0, "u.." },
	{ "__builtin_stdc_rotate_right", BI_STDC_ROTR, 0, "u.." },
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
	{ "__builtin_setjmp", BI_SETJMP, 0, "ip" }, { "__builtin_longjmp", BI_LONGJMP, 0, "vpi" },
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
	{ "__builtin_nans", BI_NANS, 1, "dS" }, { "__builtin_nansf", BI_NANS, 1, "fS" }, { "__builtin_nansl", BI_NANS, 1, "DS" },
	{ "__builtin_issignaling", BI_ISSIGNALING, 1, "i." },
	{ "__builtin_isnan", BI_ISNAN, 1, "i." }, { "__builtin_isinf", BI_ISINF, 1, "i." },
	{ "__builtin_isfinite", BI_ISFINITE, 1, "i." }, { "__builtin_signbit", BI_SIGNBIT, 1, "i." },
	{ "__builtin_fpclassify", BI_FPCLASSIFY, 1, "i." }, { "__builtin_isinf_sign", BI_ISINF_SIGN, 1, "i." },
	{ "__builtin_isnormal", BI_ISNORMAL, 1, "i." },
	{ "__builtin_isfpclass", BI_ISFPCLASS, 1, "i." },
	{ "__builtin_issubnormal", BI_ISSUBNORMAL, 1, "i." },
	{ "__builtin_iszero", BI_ISZERO, 1, "i." },
	{ "__builtin_isgreater", BI_ISGREATER, 1, "i." }, { "__builtin_isgreaterequal", BI_ISGREATEREQUAL, 1, "i." },
	{ "__builtin_isless", BI_ISLESS, 1, "i." }, { "__builtin_islessequal", BI_ISLESSEQUAL, 1, "i." },
	{ "__builtin_islessgreater", BI_ISLESSGREATER, 1, "i." }, { "__builtin_isunordered", BI_ISUNORDERED, 1, "i." },
	{ "__builtin_arm_rsr64", BI_ARM_RSR, 0, "WS" },
	{ "__builtin_rotateleft8", BI_ROTL8, 0, "CCC" }, { "__builtin_rotateleft16", BI_ROTL16, 0, "hhh" },
	{ "__builtin_rotateleft32", BI_ROTL32, 0, "uuu" }, { "__builtin_rotateleft64", BI_ROTL64, 0, "WWW" },
	{ "__builtin_rotateright8", BI_ROTR8, 0, "CCC" }, { "__builtin_rotateright16", BI_ROTR16, 0, "hhh" },
	{ "__builtin_rotateright32", BI_ROTR32, 0, "uuu" }, { "__builtin_rotateright64", BI_ROTR64, 0, "WWW" },
	/* ACLE (arm_acle.h): bit operations as the generic ones, hints, barriers */
	{ "__builtin_arm_rbit", BI_BITREVERSE32, 1, "uu" }, { "__builtin_arm_rbit64", BI_BITREVERSE64, 1, "WW" },
	{ "__builtin_arm_clz", BI_CLZ, 1, "uu" }, { "__builtin_arm_clz64", BI_CLZLL, 1, "uW" },
	{ "__builtin_arm_cls", BI_CLRSB, 1, "uu" }, { "__builtin_arm_cls64", BI_CLRSBLL, 1, "uW" },
	{ "__builtin_arm_nop", BI_ARM_NOP, 0, "v" }, { "__builtin_arm_yield", BI_ARM_YIELD, 0, "v" },
	{ "__builtin_arm_wfe", BI_ARM_WFE, 0, "v" }, { "__builtin_arm_wfi", BI_ARM_WFI, 0, "v" },
	{ "__builtin_arm_sev", BI_ARM_SEV, 0, "v" }, { "__builtin_arm_sevl", BI_ARM_SEVL, 0, "v" },
	{ "__builtin_arm_clrex", BI_ARM_CLREX, 0, "v" },
	{ "__builtin_arm_isb", BI_ARM_ISB, 0, "vu" }, { "__builtin_arm_dmb", BI_ARM_DMB, 0, "vu" },
	{ "__builtin_arm_dsb", BI_ARM_DSB, 0, "vu" }, { "__builtin_arm_trap", BI_ARM_TRAP, 0, "vu" },
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
	{ "__atomic_fetch_nand", BI_ATOMIC_FETCH_NAND, 0, "v." }, { "__atomic_nand_fetch", BI_ATOMIC_NAND_FETCH, 0, "v." },
	{ "__atomic_fetch_max", BI_ATOMIC_FETCH_MAX, 0, "v." }, { "__atomic_max_fetch", BI_ATOMIC_MAX_FETCH, 0, "v." },
	{ "__atomic_fetch_min", BI_ATOMIC_FETCH_MIN, 0, "v." }, { "__atomic_min_fetch", BI_ATOMIC_MIN_FETCH, 0, "v." },
	{ "__atomic_test_and_set", BI_ATOMIC_TEST_AND_SET, 0, "v." }, { "__atomic_clear", BI_ATOMIC_CLEAR, 0, "v." },
	/* legacy __sync_*: the __atomic_* forms with seq_cst (sig "s.": atomic_call appends the order) */
	{ "__sync_fetch_and_add", BI_ATOMIC_FETCH_ADD, 0, "s." }, { "__sync_fetch_and_sub", BI_ATOMIC_FETCH_SUB, 0, "s." },
	{ "__sync_fetch_and_and", BI_ATOMIC_FETCH_AND, 0, "s." }, { "__sync_fetch_and_or", BI_ATOMIC_FETCH_OR, 0, "s." },
	{ "__sync_fetch_and_xor", BI_ATOMIC_FETCH_XOR, 0, "s." },
	{ "__sync_add_and_fetch", BI_ATOMIC_ADD_FETCH, 0, "s." }, { "__sync_sub_and_fetch", BI_ATOMIC_SUB_FETCH, 0, "s." },
	{ "__sync_and_and_fetch", BI_ATOMIC_AND_FETCH, 0, "s." }, { "__sync_or_and_fetch", BI_ATOMIC_OR_FETCH, 0, "s." },
	{ "__sync_xor_and_fetch", BI_ATOMIC_XOR_FETCH, 0, "s." },
	{ "__sync_fetch_and_nand", BI_ATOMIC_FETCH_NAND, 0, "s." }, { "__sync_nand_and_fetch", BI_ATOMIC_NAND_FETCH, 0, "s." },
	/* int and unsigned only (clang: volatile int * / unsigned *): "si", "su" */
	{ "__sync_fetch_and_max", BI_ATOMIC_FETCH_MAX, 0, "si" }, { "__sync_fetch_and_min", BI_ATOMIC_FETCH_MIN, 0, "si" },
	{ "__sync_fetch_and_umax", BI_ATOMIC_FETCH_MAX, 0, "su" }, { "__sync_fetch_and_umin", BI_ATOMIC_FETCH_MIN, 0, "su" },
	{ "__sync_lock_test_and_set", BI_ATOMIC_EXCHANGE_N, 0, "s." },
	{ "__sync_val_compare_and_swap", BI_SYNC_VAL_CAS, 0, "s." },
	{ "__sync_bool_compare_and_swap", BI_SYNC_BOOL_CAS, 0, "s." },
	{ "__sync_lock_release", BI_SYNC_LOCK_RELEASE, 0, "s." },
	{ "__sync_synchronize", BI_SYNC_SYNCHRONIZE, 0, "s." },
	/* the sized forms (clang: the size is the operand's; the generic form checks it) and __sync_swap */
#define SYNC_N(n, bi) { n "_1", bi, 0, "s." }, { n "_2", bi, 0, "s." }, { n "_4", bi, 0, "s." }, { n "_8", bi, 0, "s." }, \
	{ n "_16", bi, 0, "s." }
	SYNC_N("__sync_fetch_and_add", BI_ATOMIC_FETCH_ADD),
	SYNC_N("__sync_fetch_and_sub", BI_ATOMIC_FETCH_SUB),
	SYNC_N("__sync_fetch_and_and", BI_ATOMIC_FETCH_AND),
	SYNC_N("__sync_fetch_and_or", BI_ATOMIC_FETCH_OR),
	SYNC_N("__sync_fetch_and_xor", BI_ATOMIC_FETCH_XOR),
	SYNC_N("__sync_add_and_fetch", BI_ATOMIC_ADD_FETCH),
	SYNC_N("__sync_sub_and_fetch", BI_ATOMIC_SUB_FETCH),
	SYNC_N("__sync_and_and_fetch", BI_ATOMIC_AND_FETCH),
	SYNC_N("__sync_or_and_fetch", BI_ATOMIC_OR_FETCH),
	SYNC_N("__sync_xor_and_fetch", BI_ATOMIC_XOR_FETCH),
	SYNC_N("__sync_fetch_and_nand", BI_ATOMIC_FETCH_NAND),
	SYNC_N("__sync_nand_and_fetch", BI_ATOMIC_NAND_FETCH),
	SYNC_N("__sync_lock_test_and_set", BI_ATOMIC_EXCHANGE_N),
	SYNC_N("__sync_val_compare_and_swap", BI_SYNC_VAL_CAS),
	SYNC_N("__sync_bool_compare_and_swap", BI_SYNC_BOOL_CAS),
	SYNC_N("__sync_lock_release", BI_SYNC_LOCK_RELEASE),
	SYNC_N("__sync_swap", BI_ATOMIC_EXCHANGE_N),
#undef SYNC_N
	{ "__sync_swap", BI_ATOMIC_EXCHANGE_N, 0, "s." },
	/* C11 _Atomic builtins (stdatomic.h): the __atomic_* forms, orders explicit */
	{ "__c11_atomic_load", BI_ATOMIC_LOAD_N, 0, "v." },
	{ "__c11_atomic_store", BI_ATOMIC_STORE_N, 0, "v." },
	{ "__c11_atomic_exchange", BI_ATOMIC_EXCHANGE_N, 0, "v." },
	{ "__c11_atomic_fetch_add", BI_ATOMIC_FETCH_ADD, 0, "v." },
	{ "__c11_atomic_fetch_sub", BI_ATOMIC_FETCH_SUB, 0, "v." },
	{ "__c11_atomic_fetch_and", BI_ATOMIC_FETCH_AND, 0, "v." },
	{ "__c11_atomic_fetch_or", BI_ATOMIC_FETCH_OR, 0, "v." },
	{ "__c11_atomic_fetch_xor", BI_ATOMIC_FETCH_XOR, 0, "v." },
	{ "__c11_atomic_fetch_nand", BI_ATOMIC_FETCH_NAND, 0, "v." },
	{ "__c11_atomic_fetch_max", BI_ATOMIC_FETCH_MAX, 0, "v." },
	{ "__c11_atomic_fetch_min", BI_ATOMIC_FETCH_MIN, 0, "v." },
	/* (p, expected, desired, success, failure): __atomic_compare_exchange_n with weak inserted ("vc") */
	{ "__c11_atomic_compare_exchange_strong", BI_ATOMIC_CMPXCHG_N, 0, "vc" },
	{ "__c11_atomic_compare_exchange_weak", BI_ATOMIC_CMPXCHG_N, 0, "vc" },
	{ "__c11_atomic_is_lock_free", BI_ATOMIC_LOCK_FREE, 1, "bz" },
	{ "__c11_atomic_init", BI_ATOMIC_STORE_N, 0, "s." },
	{ "__c11_atomic_thread_fence", BI_ATOMIC_THREAD_FENCE, 0, "vi" },
	{ "__c11_atomic_signal_fence", BI_ATOMIC_SIGNAL_FENCE, 0, "vi" },
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

/* the prototype clang gives an implicitly declared library function name (no
 * declaration made): vcc's signature letters (*clang_enc 0, see sigtype) or
 * Builtins.def's (1); NULL: not one */
const char *cc_lib_sig(const char *name, int *clang_enc)
{
	char b[128];
	size_t n = strlen(name);

	if (n + 11 > sizeof b)
		return NULL;
	memcpy(b, "__builtin_", 10);
	memcpy(b + 10, name, n + 1);
	for (uint32_t i = 0; builtins[i].name; i++)
		if (builtins[i].id == BI_LIB && !strcmp(builtins[i].name, b)) {
			*clang_enc = 0;
			return builtins[i].sig;
		}
	*clang_enc = 1;
	return lib_builtin_sig(b);
}

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
	case 'F': return ty_vector(c, prim(TY_FLOAT), 16);
	case 'H': return ty_vector(c, prim(TY_SHORT), 16);
	case 'D': return prim(TY_LDOUBLE);
	case 'p': return ty_ptr(c, prim(TY_VOID));
	case 'P': return ty_ptr(c, QT(TY_VOID, Q_CONST));
	case 's': return ty_ptr(c, prim(TY_CHAR));
	case 'S': return ty_ptr(c, QT(TY_CHAR, Q_CONST));
	case 'V':	/* a parameter: x86-64's array type is adjusted to the element pointer */
		return KIND(c, c->t_valist) == TY_ARRAY ? ty_ptr(c, T(c, c->t_valist)->base) : c->t_valist;
	default: abort();
	}
}

/* the function type of a signature in vcc's letters (sigtype) */
VPRIV qty sig_func_type(cc *c, const char *sig)
{
	qty par[16];
	uint32_t n = 0;
	int fl = 0;

	for (const char *s = sig + 1; *s; s++) {
		if (*s == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		par[n++] = sigtype(c, *s);
	}
	return ty_func(c, sigtype(c, sig[0]), par, n, fl);
}

static qty builtin_type(cc *c, const builtin *b)
{
	return sig_func_type(c, b->sig);
}

/* ---------------- expressions: builtins ---------------- */

static uint32_t *bi_dir[NDIR];

/* __has_builtin for the names the front end implements (the pp asks through pp_builtin_known) */
static int builtin_known(const char *n)
{
	for (uint32_t i = 0; builtins[i].name; i++)
		if (!strcmp(builtins[i].name, n))
			return builtins[i].id != BI_IS_CONSTANT_EVALUATED;	/* clang: C++ only */
	return lib_builtin_known(n);
}

__attribute__((constructor)) static void builtin_hook(void)
{
	pp_builtin_known = builtin_known;
}

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

/* Object-size queries never evaluate their pointer operand. Track an address's
   enclosing allocation and nearest subobject independently, so modes 0/2 and
   1/3 retain their distinct bounds through casts and pointer arithmetic. */
typedef struct object_bound {
	i128 offset, whole, sub;
	qty storage;
} object_bound;

static uint64_t object_init_extent(cc *c, qty ty, uint32_t init, unsigned depth)
{
	uint64_t size = ty_size(c, ty);
	if (!init || depth > 64)
		return size;
	const node *x = N(init);
	if (is_array(c, ty) && x->op == N_STR) {
		const cstr *s = &c->strs[x->a];
		uint64_t n = (uint64_t)s->n * s->esz;
		return (T(c, ty)->flags & TF_INCOMPLETE) && n > size ? n : size;
	}
	if (x->op != N_INIT)
		return size;
	const uint32_t *v = &c->lists[x->b];
	if (x->aux == 1) {
		if (!(T(c, ty)->flags & TF_INCOMPLETE))
			return size;
		uint64_t es = ty_size(c, T(c, ty)->base);
		for (uint32_t i = 0; i < x->c; i++) {
			uint64_t end = ((uint64_t)v[3 * i + 1] + 1) * es;
			if (end > size) size = end;
		}
	} else if (is_record(c, ty)) {
		const tag *r = &c->tags[T(c, ty)->aux];
		for (uint32_t i = 0; i < x->c && i < r->nfield; i++) {
			const field *f = &r->f[i];
			if (!v[i] || f->bitfield || (x->aux == 2 && i != x->a))
				continue;
			uint64_t end = f->off / 8 + object_init_extent(c, f->ty, v[i], depth + 1);
			if (end > size) size = end;
		}
	}
	return size;
}

static uint64_t object_element_size(cc *c, qty pointer)
{
	qty base = T(c, pointer)->base;
	return KIND(c, base) == TY_VOID || KIND(c, base) == TY_FUNC ? 1 : ty_size(c, base);
}

static bool object_pointer_bound(cc *c, uint32_t n, object_bound *b, unsigned depth);

static bool object_lvalue_bound(cc *c, uint32_t n, object_bound *b, unsigned depth)
{
	if (!n || depth > 64)
		return false;
	const node *x = N(n);
	cval index;
	switch (x->op) {
	case N_VAR:
	case N_COMPLIT: {
		const sym *s = &c->syms[x->op == N_VAR ? x->a : x->b];
		if (s->kind != S_VAR || (is_array(c, s->ty) &&
		    ((T(c, s->ty)->flags & TF_VLA) || ((T(c, s->ty)->flags & TF_INCOMPLETE) && !s->body))))
			return false;
		*b = (object_bound){ .whole = object_init_extent(c, s->ty, s->body, 0), .storage = s->ty };
		b->sub = b->whole;
		return true;
	}
	case N_STR:
		*b = (object_bound){ .whole = (uint64_t)c->strs[x->a].n * c->strs[x->a].esz };
		b->sub = b->whole;
		return true;
	case N_MEMBER: {
		const field *f = node_field(c, x);
		if (f->bitfield || !object_lvalue_bound(c, x->a, b, depth + 1))
			return false;
		b->offset += f->off / 8;
		/* Clang treats a trailing array as extending through its enclosing
		   allocation, even when a fixed bound is written (the struct hack).
		   A nested record's endpoint was retained by the enclosing member. */
		if (!(is_array(c, f->ty) && x->c + 1 == c->tags[x->b].nfield))
			b->sub = b->offset + ty_size(c, f->ty);
		b->storage = f->ty;
		return true;
	}
	case N_INDEX:
		if (!object_pointer_bound(c, x->a, b, depth + 1) || !eval(c, x->b, &index) || index.kind != CV_INT)
			return false;
		b->offset += index.i * object_element_size(c, N(x->a)->ty);
		if (is_record(c, x->ty) && b->storage &&
		    (QUNQ(b->storage) == QUNQ(x->ty) ||
		     (is_array(c, b->storage) && QUNQ(T(c, b->storage)->base) == QUNQ(x->ty))))
			b->sub = b->offset + ty_size(c, x->ty);
		if (b->storage && is_array(c, b->storage))
			b->storage = T(c, b->storage)->base;
		return true;
	case N_DEREF:
		return object_pointer_bound(c, x->a, b, depth + 1);
	default:
		return false;
	}
}

static bool object_pointer_bound(cc *c, uint32_t n, object_bound *b, unsigned depth)
{
	if (!n || depth > 64)
		return false;
	const node *x = N(n);
	cval index;
	if (x->op == N_VAR && (c->syms[x->a].flags & SF_BOS_INIT))
		return object_pointer_bound(c, c->syms[x->a].body, b, depth + 1);
	if (x->op == N_CALL && !c->o->no_builtin) {
		uint32_t callee = x->a;
		while (N(callee)->op == N_CAST || N(callee)->op == N_ADDR)
			callee = N(callee)->a;
		if (N(callee)->op != N_VAR)
			return false;
		const sym *s = &c->syms[N(callee)->a];
		if (s->kind != S_FUNC || !(s->flags & SF_GLOBAL) || s->sc == SC_STATIC || s->asmname ||
		    lib_builtin_disabled(c->o, idstr_(s->name), idlen_(s->name)))
			return false;
		const char *name = idstr_(s->name);
		uint32_t ai = 0;
		bool multiply = false;
		if (idlen_(s->name) == 6 && !memcmp(name, "malloc", 6)) { }
		else if (idlen_(s->name) == 6 && !memcmp(name, "calloc", 6)) multiply = true;
		else if (idlen_(s->name) == 7 && !memcmp(name, "realloc", 7)) ai = 1;
		else return false;
		const ctype *ft = T(c, s->ty);
		uint32_t nargs = multiply || ai ? 2 : 1;
		if (ft->kind != TY_FUNC || (ft->flags & (TF_NOPROTO | TF_VARIADIC)) ||
		    ft->n != nargs || x->c != nargs || !is_ptr(c, ft->base) || !is_void(c, T(c, ft->base)->base))
			return false;
		for (uint32_t i = 0; i < nargs; i++) {
			qty p = c->params[ft->aux + i];
			if (ai && i == 0 ? (!is_ptr(c, p) || !is_void(c, T(c, p)->base)) : QUNQ(p) != QUNQ(c->t_size))
				return false;
		}
		if (!eval(c, c->lists[x->b + ai], &index) || index.kind != CV_INT || index.i < 0)
			return false;
		u128 size = (u128)index.i;
		if (multiply) {
			if (!eval(c, c->lists[x->b + 1], &index) || index.kind != CV_INT || index.i < 0)
				return false;
			size *= (u128)index.i;
		}
		if (size > (ty_size(c, c->t_size) == 4 ? UINT32_MAX : UINT64_MAX))
			return false;
		*b = (object_bound){ .whole = (i128)size, .sub = (i128)size };
		return true;
	}
	if (x->op == N_ADDR || (x->op == N_CAST && x->aux == CK_DECAY))
		return object_lvalue_bound(c, x->a, b, depth + 1);
	if (x->op == N_CAST && is_ptr(c, N(x->a)->ty)) {
		if (!object_pointer_bound(c, x->a, b, depth + 1))
			return false;
		if (is_ptr(c, x->ty) && is_record(c, T(c, x->ty)->base) &&
		    QUNQ(T(c, x->ty)->base) != QUNQ(T(c, N(x->a)->ty)->base))
			b->sub = b->whole;
		return true;
	}
	if (is_array(c, x->ty))
		return object_lvalue_bound(c, n, b, depth + 1);
	if (x->op == N_BINARY && (x->aux == T_PLUS || x->aux == T_MINUS)) {
		uint32_t p = x->a, i = x->b;
		if (!is_ptr(c, N(p)->ty) && x->aux == T_PLUS) p = x->b, i = x->a;
		if (!is_ptr(c, N(p)->ty) || !object_pointer_bound(c, p, b, depth + 1) ||
		    !eval(c, i, &index) || index.kind != CV_INT)
			return false;
		b->offset += (x->aux == T_MINUS ? -index.i : index.i) * object_element_size(c, N(p)->ty);
		return true;
	}
	return false;
}

VPRIV bool eval_builtin(cc *c, const node *x, cval *v)
{
	const uint32_t *arg = &c->lists[x->b];
	cval a;

	v->kind = CV_INT;
	v->ice = true;
	switch (x->aux) {
	case BI_OBJECT_SIZE: case BI_DYNAMIC_OBJECT_SIZE: {
		object_bound b;
		if (!eval(c, arg[1], &a) || a.kind != CV_INT)
			return false;
		if (object_pointer_bound(c, arg[0], &b, 0)) {
			const i128 end = (a.i & 1) && b.sub < b.whole ? b.sub : b.whole;
			v->i = b.offset < 0 || b.offset > end ? 0 : end - b.offset;
		} else
			v->i = a.i & 2 ? 0 : (i128)(uint64_t)-1;
		v->ice = false;
		return true;
	}
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
	case BI_NAN: case BI_NANF: case BI_NANS: {
		uint32_t n = arg[0];
		while (N(n)->op == N_CAST)
			n = N(n)->a;
		if (N(n)->op != N_STR)
			return false;
		const char *text = c->strs[N(n)->a].s;
		int p = KIND(c, x->ty) == TY_FLOAT16 ? 10 : KIND(c, x->ty) == TY_FLOAT ? 23 : KIND(c, x->ty) == TY_DOUBLE ? 52 :
		        c->o->target == PP_DARWIN ? 52 : c->o->target == PP_LINUX_X86_64 ? 63 : 112;
		u128 payload = 0;
		unsigned base = 10;
		bool valid = true;
		if (text[0] == '0') {
			base = 8;
			if (text[1] == 'x' || text[1] == 'X')
				base = 16, text += 2;
			if (base == 16 && !*text)
				valid = false;
		}
		for (; *text; text++) {
			unsigned digit = *text >= '0' && *text <= '9' ? (unsigned)(*text - '0') :
			                 *text >= 'a' && *text <= 'f' ? (unsigned)(*text - 'a' + 10) :
			                 *text >= 'A' && *text <= 'F' ? (unsigned)(*text - 'A' + 10) : base;
			if (digit >= base || payload > (UINT64_MAX - digit) / base) { valid = false; break; }
			payload = payload * base + digit;
		}
		if (!valid)
			return false;
		payload &= (((u128)1 << (p - 1)) - 1);
		if (x->aux == BI_NANS) {
			if (!payload)
				payload = (u128)1 << (p - 2);
		} else
			payload |= (u128)1 << (p - 1);
		v->q = (u128)0x7fff << 112 | payload << (112 - p);
		v->kind = CV_FLT, v->ld = true, v->f = q_to_d(v->q), v->ice = false;
		return true;
	}
	case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_ISNORMAL: case BI_SIGNBIT: {
		if (!eval(c, arg[0], &a) || a.kind != CV_FLT)
			return false;
		u128 q = a.ld ? a.q : q_of_d(a.f);
		unsigned exponent = (unsigned)(q >> 112) & 0x7fff;
		int kind = KIND(c, N(arg[0])->ty);
		unsigned minimum = kind == TY_FLOAT16 ? 16383 - 14 : kind == TY_FLOAT || kind == TY_BFLOAT16 ? 16383 - 126 :
		                   kind == TY_DOUBLE || c->o->target == PP_DARWIN ? 16383 - 1022 : 1;
		switch (x->aux) {
		case BI_ISNAN: v->i = q_isnan(q); break;
		case BI_ISINF: v->i = q_isinf(q); break;
		case BI_ISFINITE: v->i = exponent != 0x7fff; break;
		case BI_ISNORMAL: v->i = exponent >= minimum && exponent != 0x7fff; break;
		default: v->i = q >> 127; break;
		}
		v->ice = false;
		return true;
	}
	case BI_ISSIGNALING:
		if (!eval(c, arg[0], &a) || a.kind != CV_FLT)
			return false;
		v->i = a.ld ? q_isnan(a.q) && !(a.q & ((u128)1 << 111)) : 0;
		v->ice = false;
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
	uint32_t open = tok(c)->loc;

	vinit(args);
	expect(c, T_LPAREN, "'('");
	if (!is(c, T_RPAREN)) {
		do
			vpush(c, args, assign_expr(c));
		while (accept(c, T_COMMA));
	}
	c->args_rp = tok(c)->loc;
	if (unlikely(!is(c, T_RPAREN))) {
		/* clang: a call without its ')' is invalid; the last argument says so */
		if (args->n && ISERR(args->v[args->n - 1]))
			skip_close(c, T_RPAREN);
		else
			expect_match_fail(c, T_RPAREN, open);
		if (args->n)
			args->v[args->n - 1] = err_node(c, open);
		else
			vpush(c, args, err_node(c, open));
	} else {
		next(c);
	}
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
	if (c->fn && (id == BI_OBJECT_SIZE || id == BI_DYNAMIC_OBJECT_SIZE))
		c->syms[c->fn].flags |= SF_BOS_QUERY;
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
			uint32_t path[FM_DEPTH], ti;
			int d;

			if (!is(c, T_IDENT))
				fatal(c, tok(c)->loc, "expected identifier");
			if (!is_record(c, t)) {
				cc_error(c, tok(c)->loc, "offsetof requires struct, union, or class type, '%s' invalid", tyname(c, t));
				next(c);
				break;
			}
			ti = T(c, t)->aux;
			d = find_member(c, ti, tok(c)->id, path, 0);
			if (d < 0) {
				cc_error(c, tok(c)->loc, "no member named '%.*s' in '%s'", IDF(tok(c)->id), tyname(c, t));
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
				cc_error(c, loc, "subscripted value is not an array ('%s' invalid)", tyname(c, t));
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
	expect_here(c, T_RPAREN, "')'");
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
			       id == BI_SYNC_VAL_CAS || id == BI_SYNC_BOOL_CAS ? 4 : id == BI_SYNC_SYNCHRONIZE ? 1 :
			       id == BI_ATOMIC_TEST_AND_SET || id == BI_ATOMIC_CLEAR ? 2 : 3;
	vec args;
	qty pt, t, rt = prim(TY_VOID), tp;

	args_list(c, &args);
	for (uint32_t i = 0; i < args.n; i++)
		args.v[i] = decay(c, args.v[i]);
	if (id == BI_SYNC_SYNCHRONIZE) {	/* a full barrier: __atomic_thread_fence(seq_cst) */
		if (args.n)
			arity(c, loc, 0, &args);
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
	if (b->sig[1] == 'c' && args.n == 5) {	/* __c11_atomic_compare_exchange_*: weak goes before the orders */
		vpush(c, &args, args.v[4]);
		args.v[4] = args.v[3];
		args.v[3] = mk_int(c, 0, prim(TY_INT), loc);
	}
	if (b->sig[0] == 's' && (id == BI_ATOMIC_FETCH_NAND || id == BI_ATOMIC_NAND_FETCH))
	{
		pp_span r = { loc, pp_tok_end(c->p, loc) };	/* the builtin's name */

		cc_warnx(c, "sync-fetch-and-nand-semantics-changed", true, NULL, c->args_rp, &r, 1, NULL, 0,
			 "the semantics of this intrinsic changed with GCC version 4.4 - the newer semantics are provided here");
	}
	if ((int)args.n != want) {
		arity(c, loc, (unsigned)want, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	pt = N(args.v[0])->ty;
	if (!is_ptr(c, pt)) {
		pp_span r = rng(c, args.v[0]);

		report(c, "error", loc, &r, 1, NULL, 0, "address argument to atomic builtin must be a pointer ('%s' invalid)", tyname(c, pt));
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	if (b->sig[1] == 'i' || b->sig[1] == 'u') {	/* __sync_fetch_and_{,u}{max,min}: volatile int * / unsigned * */
		qty it = prim(b->sig[1] == 'i' ? TY_INT : TY_UINT);

		args.v[0] = convert(c, args.v[0], ty_ptr(c, (it | Q_VOLATILE)));
		pt = N(args.v[0])->ty;
	}
	if (id == BI_ATOMIC_TEST_AND_SET || id == BI_ATOMIC_CLEAR) {
		/* (p, order) on the byte at p: __atomic_exchange_n(p, 1, o) != 0 / __atomic_store_n(p, 0, o) */
		qty bt = prim(TY_UCHAR);
		uint32_t r;
		vec a;

		vinit(&a);
		vpush(c, &a, cast_node(c, args.v[0], ty_ptr(c, (bt | (QQ(T(c, pt)->base) & Q_VOLATILE))), CK_NOP));
		vpush(c, &a, mk_int(c, id == BI_ATOMIC_CLEAR ? 0 : 1, bt, loc));
		vpush(c, &a, convert(c, args.v[1], prim(TY_INT)));
		if (id == BI_ATOMIC_CLEAR)
			return mk_builtin(c, BI_ATOMIC_STORE_N, prim(TY_VOID), loc, &a);
		r = mk_builtin(c, BI_ATOMIC_EXCHANGE_N, bt, loc, &a);
		return convert(c, r, prim(TY_BOOL));
	}
	t = QUNQ(T(c, pt)->base);
	tp = ty_ptr(c, t);
	if (!(T(c, t)->size == 1 || T(c, t)->size == 2 || T(c, t)->size == 4 || T(c, t)->size == 8 || T(c, t)->size == 16)) {
		cc_error(c, loc, "address argument to atomic builtin must be a pointer to 1,2,4,8 or 16 byte type ('%s' invalid)", tyname(c, pt));
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
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = convert(c, args.v[1], t);
		args.v[2] = convert(c, args.v[2], t);
		rt = id == BI_SYNC_VAL_CAS ? t : prim(TY_BOOL);
		break;
	case BI_ATOMIC_FETCH_MAX: case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH:
		if (!is_integer(c, t) || KIND(c, t) == TY_BOOL)	/* floating max/min: not supported yet */
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_NAND_FETCH:
		if (!is_integer(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer");
		args.v[1] = convert(c, args.v[1], t);
		rt = t;
		break;
	default:	/* fetch_op / op_fetch */
		if (!is_integer(c, t) && !is_ptr(c, t))
			cc_error(c, loc, "address argument to atomic operation must be a pointer to integer or pointer ('%s' invalid)", tyname(c, pt));
		args.v[1] = convert(c, args.v[1], is_ptr(c, t) ? c->t_ptrdiff : t);
		rt = t;
		break;
	}
	for (int i = want - (id == BI_ATOMIC_CMPXCHG_N || id == BI_ATOMIC_CMPXCHG ? 2 : 1); i < want; i++)
		args.v[i] = convert(c, args.v[i], prim(TY_INT));
	return mk_builtin(c, id, rt, loc, &args);
}

/* A fixed-width bit operation used by the generic forms. */
static uint32_t fixed_bitcount(cc *c, int id, uint32_t x, bool wide, uint32_t loc)
{
	vec args;
	int fid = id == BI_CLZG ? (wide ? BI_CLZLL : BI_CLZ) :
	          id == BI_CTZG ? (wide ? BI_CTZLL : BI_CTZ) :
	                         (wide ? BI_POPCOUNTLL : BI_POPCOUNT);
	vinit(&args);
	vpush(c, &args, x);
	return mk_builtin(c, fid, prim(TY_INT), loc, &args);
}

/* Generic bit counts use the operand's precision, without integer promotion.
 * Save effectful operands before selecting a half or the zero fallback; both
 * arguments to a builtin call are evaluated even when the fallback is unused. */
static uint32_t generic_bitcount(cc *c, int id, uint32_t loc)
{
	vec args;
	uint32_t x, r, pre, fbpre = 0, fb = 0;
	qty xt;
	unsigned bits, width;

	args_list(c, &args);
	if (args.n < 1 || args.n > (id == BI_POPCOUNTG ? 1u : 2u)) {
		arity(c, loc, id == BI_POPCOUNTG ? 1u : args.n ? 2u : 1u, &args);
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	x = decay(c, args.v[0]);
	xt = QUNQ(N(x)->ty);
	if (!is_integer(c, xt) || is_signed(c, xt) || int_bits(c, xt) > 128) {
		cc_error(c, N(x)->loc, "1st argument must be a scalar unsigned integer type of at most 128 bits");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	bits = KIND(c, xt) == TY_BOOL ? 1 : int_bits(c, xt);
	width = bits > 64 ? 128 : bits > 32 ? 64 : 32;
	x = cplx_reuse(c, x, &pre);
	if (args.n == 2) {
		uint32_t y = decay(c, args.v[1]);
		qty ft = promoted(c, y);
		if (KIND(c, ft) != TY_INT || T(c, ft)->n) {
			cc_error(c, N(y)->loc, "2nd argument must have type int after integer promotion");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		fb = cplx_reuse(c, convert(c, y, prim(TY_INT)), &fbpre);
	}
	if (width == 128) {
		uint32_t w = convert(c, x, prim(TY_UINT128));
		uint32_t lo = convert(c, w, prim(TY_ULLONG));
		uint32_t hi = convert(c, binop(c, T_GREATERGREATER, w,
		                             mk_int(c, 64, prim(TY_INT), loc), loc), prim(TY_ULLONG));
		uint32_t low = fixed_bitcount(c, id, lo, true, loc);
		uint32_t high = fixed_bitcount(c, id, hi, true, loc);
		if (id == BI_POPCOUNTG)
			r = binop(c, T_PLUS, low, high, loc);
		else {
			uint32_t first = id == BI_CLZG ? hi : lo;
			uint32_t count = id == BI_CLZG ? high : low;
			uint32_t other = id == BI_CLZG ? low : high;
			uint32_t cond = binop(c, T_EXCLAIMEQ, first, mk_int(c, 0, prim(TY_ULLONG), loc), loc);
			r = conditional(c, cond, count,
			                binop(c, T_PLUS, mk_int(c, 64, prim(TY_INT), loc), other, loc), loc);
		}
	} else {
		uint32_t w = convert(c, x, prim(width == 64 ? TY_ULLONG : TY_UINT));
		r = fixed_bitcount(c, id, w, width == 64, loc);
	}
	if (id == BI_CLZG && bits < width)
		r = binop(c, T_MINUS, r, mk_int(c, width - bits, prim(TY_INT), loc), loc);
	if (args.n == 2)
		r = conditional(c, binop(c, T_EXCLAIMEQ, x, mk_int(c, 0, xt, loc), loc), r, fb, loc);
	return with_pre(c, pre, with_pre(c, fbpre, r));
}

/*
 * __builtin_shufflevector(a, b, i...): lanes of a then b picked by constant
 * indices (-1: undefined, here 0), a vector of as many lanes;
 * __builtin_convertvector(v, T): each lane converted to T's lane type.
 * Both lowered lane by lane in emit (builtin.c).
 */
VPRIV uint32_t vector_builtin(cc *c, bool shuffle, uint32_t loc)
{
	vec args;
	uint32_t a, b;
	qty at, rt;

	vinit(&args);
	expect(c, T_LPAREN, "'('");
	a = rvalue(c, assign_expr(c));
	at = QUNQ(N(a)->ty);
	expect(c, T_COMMA, "','");
	if (!shuffle) {
		rt = type_name(c);
		expect_here(c, T_RPAREN, "')'");
		if (!is_vector(c, at) || !is_vector(c, rt) ||
		    ty_size(c, at) / ty_size(c, T(c, at)->base) != ty_size(c, rt) / ty_size(c, T(c, rt)->base)) {
			cc_error(c, loc, "first two arguments to __builtin_convertvector must have the same number of elements");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		vpush(c, &args, a);
		return mk_builtin(c, BI_CONVERTVECTOR, rt, loc, &args);
	}
	b = rvalue(c, assign_expr(c));
	vpush(c, &args, a);
	vpush(c, &args, b);
	if (!is_vector(c, at) || !compatible(c, at, QUNQ(N(b)->ty))) {
		cc_error(c, loc, "first two arguments to '__builtin_shufflevector' must have the same type");
		while (accept(c, T_COMMA))
			assign_expr(c);
		expect_here(c, T_RPAREN, "')'");
		return mk_int(c, 0, prim(TY_INT), loc);
	}
	{
		qty et = T(c, at)->base;
		uint64_t n = ty_size(c, at) / ty_size(c, et), k = 0;

		while (accept(c, T_COMMA)) {
			uint32_t ix = assign_expr(c);
			i128 v = 0;

			if (!const_int(c, ix, &v) || v < -1 || v >= (i128)(2 * n))
				cc_error(c, N(ix)->loc, "index for __builtin_shufflevector must be a constant integer in range");
			vpush(c, &args, mk_int(c, v, prim(TY_INT), N(ix)->loc));
			k++;
		}
		expect_here(c, T_RPAREN, "')'");
		if (k == 0 || (k & (k - 1)) || k * ty_size(c, et) > 64) {
			cc_error(c, loc, "__builtin_shufflevector: the number of indices must be a power of two");
			return mk_int(c, 0, prim(TY_INT), loc);
		}
		rt = ty_vector(c, et, (uint32_t)(k * ty_size(c, et)));
	}
	return mk_builtin(c, BI_SHUFFLEVECTOR, rt, loc, &args);
}

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

/* SSE operations expressed in ordinary vector lanes, preserving argument
 * evaluation and the instruction's second-operand choice for NaNs and ties. */
static uint32_t builtin_vector_lane(cc *c, uint32_t v, qty et, unsigned i, uint32_t loc)
{
	uint32_t p = cast_node(c, rvalue(c, v), ty_ptr(c, et), CK_VADDR);
	uint32_t n = mk2(c, N_INDEX, et, loc, p, mk_int(c, i, prim(TY_LONG), loc));
	N(n)->flags = NF_LVALUE;
	return n;
}

static uint32_t x86_simd_call(cc *c, const builtin *b, vec *args, qty rt, uint32_t loc)
{
	const char *name = b->name + sizeof("__builtin_ia32_") - 1;
	if (!strcmp(name, "shufps")) {
		i128 imm;
		if (args->n != 3 || !const_int(c, args->v[2], &imm) || imm < 0 || imm > 255) {
			cc_error(c, loc, "argument to '%s' must be a constant integer in range [0, 255]", b->name);
			return err_node(c, loc);
		}
		vec lanes;
		vinit(&lanes);
		vpush(c, &lanes, args->v[0]); vpush(c, &lanes, args->v[1]);
		for (unsigned i = 0; i < 4; i++)
			vpush(c, &lanes, mk_int(c, ((unsigned)imm >> (2 * i) & 3) + (i >= 2 ? 4 : 0), prim(TY_INT), loc));
		return mk_builtin(c, BI_SHUFFLEVECTOR, rt, loc, &lanes);
	}
	if (!strcmp(name, "movmskps")) {
		if (args->n != 1) return err_node(c, loc);
		qty vt = N(args->v[0])->ty;
		uint32_t sym = sym_new(c, S_VAR, 0, vt, loc);
		c->syms[sym].sc = SC_AUTO;
		uint32_t saved = mk(c, N_VAR, vt, loc);
		N(saved)->a = sym; N(saved)->flags = NF_LVALUE;
		uint32_t result = mk_int(c, 0, prim(TY_INT), loc);
		for (unsigned i = 0; i < 4; i++) {
			vec one;
			vinit(&one);
			vpush(c, &one, rvalue(c, builtin_vector_lane(c, saved, prim(TY_FLOAT), i, loc)));
			uint32_t sign = mk_builtin(c, BI_SIGNBIT, prim(TY_INT), loc, &one);
			sign = convert(c, convert(c, sign, prim(TY_BOOL)), prim(TY_INT));
			sign = mk_bin(c, T_LESSLESS, prim(TY_INT), sign, mk_int(c, i, prim(TY_INT), loc), loc);
			result = mk_bin(c, T_PIPE, prim(TY_INT), result, sign, loc);
		}
		return with_pre(c, mk2(c, N_ASSIGN, vt, loc, saved, args->v[0]), result);
	}
	if (args->n != 2) return err_node(c, loc);
	bool shift = name[0] == 'p', cmp = name[0] == 'c';
	qty et = shift ? prim(TY_USHORT) : cmp ? prim(TY_INT) : prim(TY_FLOAT);
	qty vt = ty_vector(c, et, 16);
	uint32_t saved[2], pre[2];
	for (unsigned i = 0; i < 2; i++) {
		qty t = N(args->v[i])->ty;
		uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
		c->syms[sym].sc = SC_AUTO;
		saved[i] = mk(c, N_VAR, t, loc); N(saved[i])->a = sym; N(saved[i])->flags = NF_LVALUE;
		pre[i] = mk2(c, N_ASSIGN, t, loc, saved[i], args->v[i]);
	}
	uint32_t sym = sym_new(c, S_VAR, 0, vt, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t out = mk(c, N_VAR, vt, loc); N(out)->a = sym; N(out)->flags = NF_LVALUE;
	uint32_t result = cast_node(c, rvalue(c, out), rt, CK_NOP);
	for (unsigned i = 16 / ty_size(c, et); i--;) {
		uint32_t a = rvalue(c, builtin_vector_lane(c, saved[0], shift ? et : prim(TY_FLOAT), i, loc));
		uint32_t value;
		if (shift) {
			uint32_t count = convert(c, rvalue(c, saved[1]), prim(TY_UINT));
			uint32_t valid = mk_bin(c, T_LESS, prim(TY_INT), count, mk_int(c, 16, prim(TY_UINT), loc), loc);
			value = mk_bin(c, name[2] == 'l' ? T_LESSLESS : T_GREATERGREATER, prim(TY_UINT), convert(c, a, prim(TY_UINT)), count, loc);
			value = conditional(c, valid, value, mk_int(c, 0, prim(TY_UINT), loc), loc);
			value = convert(c, value, et);
		} else {
			uint32_t rhs = rvalue(c, builtin_vector_lane(c, saved[1], prim(TY_FLOAT), i, loc));
			uint32_t cond = mk_bin(c, cmp ? T_LESSEQ : !strcmp(name, "maxps") ? T_GREATER : T_LESS, prim(TY_INT), a, rhs, loc);
			value = cmp ? conditional(c, cond, mk_int(c, 0, et, loc), mk_int(c, -1, et, loc), loc) : conditional(c, cond, a, rhs, loc);
		}
		uint32_t dst = builtin_vector_lane(c, out, et, i, loc);
		result = with_pre(c, mk2(c, N_ASSIGN, et, loc, dst, value), result);
	}
	return with_pre(c, pre[0], with_pre(c, pre[1], result));
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
	for (const builtin *b = builtins; b->name; b++) {
		if (strcmp(b->name, target))
			continue;
		qty ft = builtin_type(c, b);
		uint32_t s = sym_new(c, S_BUILTIN, 0, ft, loc);
		c->syms[s].val = b - builtins;
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

/* abs has no integer promotions and INT_MIN maps to itself. Do the
 * negation in the unsigned type so that the generated operation cannot
 * acquire signed-overflow semantics. */
static uint32_t elementwise_abs_lane(cc *c, uint32_t a, qty et, uint32_t loc)
{
	if (is_integer(c, et)) {
		qty u = unsigned_of(c, et);
		uint32_t neg = convert(c, mk_bin(c, T_MINUS, u, mk_int(c, 0, u, loc), convert(c, a, u), loc), et);
		uint32_t cond = mk_bin(c, T_LESS, prim(TY_INT), a, mk_int(c, 0, et, loc), loc);
		return convert(c, conditional(c, cond, neg, a, loc), et);
	}
	if (ty_size(c, et) == 2) {
		/* Clearing the sign bit directly also preserves signaling NaNs;
		 * converting through float would quiet them. */
		uint32_t sym = sym_new(c, S_VAR, 0, et, loc);
		c->syms[sym].sc = SC_AUTO;
		uint32_t tmp = mk(c, N_VAR, et, loc); N(tmp)->a = sym; N(tmp)->flags = NF_LVALUE;
		uint32_t p = cast_node(c, addr_of(c, tmp, loc), ty_ptr(c, prim(TY_USHORT)), CK_P2P);
		uint32_t bits = mk(c, N_DEREF, prim(TY_USHORT), loc); N(bits)->a = p; N(bits)->flags = NF_LVALUE;
		uint32_t masked = mk_bin(c, T_AMP, prim(TY_USHORT), rvalue(c, bits), mk_int(c, 0x7fff, prim(TY_USHORT), loc), loc);
		uint32_t write = mk2(c, N_ASSIGN, prim(TY_USHORT), loc, bits, masked);
		return with_pre(c, mk2(c, N_ASSIGN, et, loc, tmp, a), with_pre(c, write, rvalue(c, tmp)));
	}
	const char *name = KIND(c, et) == TY_FLOAT ? "__builtin_fabsf" : KIND(c, et) == TY_DOUBLE ? "__builtin_fabs" : "__builtin_fabsl";
	const builtin *b = builtins;
	while (strcmp(b->name, name)) b++;
	qty ft = builtin_type(c, b);
	uint32_t sym = sym_new(c, S_BUILTIN, 0, ft, loc);
	c->syms[sym].val = b - builtins;
	uint32_t fn = mk(c, N_VAR, ft, loc); N(fn)->a = sym;
	vec args; vinit(&args); vpush(c, &args, a);
	uint32_t n = mk(c, N_CALL, et, loc);
	N(n)->a = decay(c, fn); N(n)->b = vfreeze(c, &args); N(n)->c = 1;
	return n;
}

/* A folded argument may still contain evaluated comma operands. This
 * check includes volatile/atomic reads and respects short-circuiting, while
 * leaving genuinely constant arguments free of synthetic comma expressions
 * (which would make a C23 constexpr initializer nonconstant). */
static bool elementwise_abs_effects(cc *c, uint32_t n, bool load)
{
	if (!n) return false;
	const node *x = N(n);
	if (load && (x->flags & NF_LVALUE) && ((QQ(x->ty) & Q_VOLATILE) || (T(c, x->ty)->flags & TF_ATOMIC)))
		return true;
	switch (x->op) {
	case N_NUM: case N_FNUM: case N_ZERO: case N_STR: case N_VAR: case N_LABELADDR:
		return false;
	case N_ADDR: case N_MEMBER:
		return elementwise_abs_effects(c, x->a, false);
	case N_CAST:
		return elementwise_abs_effects(c, x->a, x->aux != CK_DECAY && x->aux != CK_FDECAY);
	case N_UNARY: case N_DEREF:
		return elementwise_abs_effects(c, x->a, true);
	case N_INDEX: case N_BINARY: case N_COMMA: case N_CPLX:
		return elementwise_abs_effects(c, x->a, true) || elementwise_abs_effects(c, x->b, true);
	case N_LOGAND: case N_LOGOR: case N_COND: {
		cval cv;
		if (elementwise_abs_effects(c, x->a, true)) return true;
		if (cc_eval(c, x->a, &cv)) {
			bool truth = cv.kind == CV_INT ? cv.i != 0 : cv.kind == CV_FLT ? (cv.ld ? q_nz(cv.q) : cv.f != 0) : true;
			if (x->op == N_COND)
				return elementwise_abs_effects(c, truth ? x->b : x->c, true);
			return (x->op == N_LOGAND ? truth : !truth) && elementwise_abs_effects(c, x->b, true);
		}
		return elementwise_abs_effects(c, x->b, true) || elementwise_abs_effects(c, x->c, true);
	}
	case N_BUILTIN: case N_CALL: {
		if (x->op == N_BUILTIN && x->aux == BI_CONSTANT_P) return false;
		cval value;
		if (!cc_eval(c, n, &value)) return true;
		if (x->op == N_CALL) {
			uint32_t fn = x->a;
			while (N(fn)->op == N_CAST) fn = N(fn)->a;
			if (N(fn)->op != N_VAR || c->syms[N(fn)->a].kind != S_BUILTIN) return true;
		}
		for (uint32_t i = 0; i < x->c; i++)
			if (elementwise_abs_effects(c, c->lists[x->b + i], true)) return true;
		return false;
	}
	default:
		return true;
	}
}

/* fabs/copysign are representation operations: integer masks preserve both
 * signaling NaNs and signed zero. Anonymous locals evaluate each operand once. */
VPRIV uint32_t fp_sign_call(cc *c, const char *name, size_t len, qty ft, const vec *args, uint32_t loc)
{
	if (c->o->target != PP_LINUX_X86_64 && c->o->target != PP_LINUX_AARCH64)
		return 0;
	bool copy = (len == 8 && !memcmp(name, "copysign", 8)) ||
		    (len == 9 && !memcmp(name, "copysignf", 9));
	bool abs = (len == 4 && !memcmp(name, "fabs", 4)) ||
		   (len == 5 && !memcmp(name, "fabsf", 5));
	if (!copy && !abs) return 0;
	bool single = name[len - 1] == 'f';
	qty t = prim(single ? TY_FLOAT : TY_DOUBLE);
	if ((T(c, ft)->flags & (TF_NOPROTO | TF_VARIADIC)) || args->n != (copy ? 2 : 1) ||
	    !compatible(c, ft, sig_func_type(c, copy ? (single ? "fff" : "ddd") : (single ? "ff" : "dd"))))
		return 0;
	cval a, b;
	if (!elementwise_abs_effects(c, args->v[0], true) && cc_eval(c, args->v[0], &a) && a.kind == CV_FLT &&
	    (!copy || (!elementwise_abs_effects(c, args->v[1], true) && cc_eval(c, args->v[1], &b) && b.kind == CV_FLT)))
		return mk_flt(c, __builtin_copysign(a.f, copy ? b.f : 1.0), t, loc);
	if (c->o->general_regs_only) {
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
	return with_pre(c, pre[0], result);
}

static uint32_t elementwise_abs_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	args_list(c, &args);
	if (args.n != 1) { arity(c, loc, 1, &args); return err_node(c, loc); }
	uint32_t arg = decay(c, args.v[0]);
	qty t = QUNQ(N(arg)->ty), et = is_vector(c, t) ? T(c, t)->base : t;
	int k = KIND(c, et);
	if ((!is_float(c, et) && (!is_integer(c, et) || !is_signed(c, et))) || k == TY_ENUM || k == TY_BOOL) {
		cc_error(c, loc, "argument to '%s' must be a scalar or vector of signed integer or floating-point type", b->name);
		return err_node(c, loc);
	}
	/* Retain constant-expression support for scalar integer/floating args. */
	cval cv;
	if (!is_vector(c, t) && cc_eval(c, arg, &cv) && (cv.kind == CV_FLT || cv.kind == CV_INT)) {
		/* A known value need not be a pure expression: constant evaluation
		 * of a comma expression may ignore an observable left operand.
		 * Evaluate the original exactly once, then return only its folded
		 * magnitude. Reusing arg in the integer select would evaluate it twice. */
		uint32_t folded;
		if (cv.kind == CV_FLT)
			folded = cv.ld ? mk_fltq(c, cv.q & ~((u128)1 << 127), et, loc) : mk_flt(c, __builtin_fabs(cv.f), et, loc);
		else
			folded = mk_int(c, trunc_to(c, cv.i < 0 ? (i128)(0 - (u128)cv.i) : cv.i, et), et, loc);
		return elementwise_abs_effects(c, arg, true) ? with_pre(c, arg, folded) : folded;
	}
	uint32_t sym = sym_new(c, S_VAR, 0, t, loc);
	c->syms[sym].sc = SC_AUTO;
	uint32_t saved = mk(c, N_VAR, t, loc); N(saved)->a = sym; N(saved)->flags = NF_LVALUE;
	uint32_t pre = mk2(c, N_ASSIGN, t, loc, saved, arg);
	if (!is_vector(c, t))
		return with_pre(c, pre, elementwise_abs_lane(c, rvalue(c, saved), et, loc));
	sym = sym_new(c, S_VAR, 0, t, loc); c->syms[sym].sc = SC_AUTO;
	uint32_t out = mk(c, N_VAR, t, loc); N(out)->a = sym; N(out)->flags = NF_LVALUE;
	uint32_t result = rvalue(c, out);
	for (unsigned i = ty_size(c, t) / ty_size(c, et); i--;) {
		uint32_t lane = rvalue(c, builtin_vector_lane(c, saved, et, i, loc));
		uint32_t value = elementwise_abs_lane(c, lane, et, loc);
		uint32_t dst = builtin_vector_lane(c, out, et, i, loc);
		result = with_pre(c, mk2(c, N_ASSIGN, et, loc, dst, value), result);
	}
	return with_pre(c, pre, result);
}

/* Generic rotations preserve the first operand's type and evaluate each
 * operand once. Normalize the count before narrowing it: the caller may pass
 * a 64/128-bit count even when rotating a small integer. */
static uint32_t stdc_rotate(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	uint32_t prex, precount, pren;

	args_list(c, &args);
	if (args.n != 2) {
		arity(c, loc, 2, &args);
		return err_node(c, loc);
	}
	args.v[0] = decay(c, args.v[0]);
	args.v[1] = decay(c, args.v[1]);
	qty t = QUNQ(N(args.v[0])->ty), ct = N(args.v[1])->ty;
	if (!is_integer(c, t) || is_signed(c, t)) {
		cc_error(c, N(args.v[0])->loc, "rotation operand must have unsigned integer type");
		return err_node(c, loc);
	}
	if (!is_integer(c, ct)) {
		cc_error(c, N(args.v[1])->loc, "rotation count must have integer type");
		return err_node(c, loc);
	}
	unsigned bits = KIND(c, t) == TY_BOOL ? 1 : int_bits(c, t);
	qty work = prim(bits <= 32 ? TY_UINT : bits <= 64 ? TY_ULLONG : TY_UINT128);
	unsigned countbits = int_bits(c, ct);
	ct = prim(countbits <= 32 ? TY_UINT : countbits <= 64 ? TY_ULLONG : TY_UINT128);
	uint32_t x = cplx_reuse(c, convert(c, rvalue(c, args.v[0]), work), &prex);
	uint32_t count = cplx_reuse(c, convert(c, rvalue(c, args.v[1]), ct), &precount);
	uint32_t n = binop(c, T_PERCENT, rvalue(c, count), mk_int(c, bits, ct, loc), loc);
	n = cplx_reuse(c, convert(c, n, prim(TY_UINT)), &pren);
	uint32_t reverse = binop(c, T_MINUS, mk_int(c, bits, prim(TY_UINT), loc), rvalue(c, n), loc);
	reverse = binop(c, T_PERCENT, reverse, mk_int(c, bits, prim(TY_UINT), loc), loc);
	bool left = b->id == BI_STDC_ROTL;
	uint32_t a = binop(c, left ? T_LESSLESS : T_GREATERGREATER, rvalue(c, x), rvalue(c, n), loc);
	uint32_t z = binop(c, left ? T_GREATERGREATER : T_LESSLESS, rvalue(c, x), reverse, loc);
	uint32_t value = convert(c, binop(c, T_PIPE, a, z, loc), t);
	return with_pre(c, prex, with_pre(c, precount, with_pre(c, pren, value)));
}

VPRIV uint32_t builtin_call(cc *c, const builtin *b, uint32_t loc)
{
	vec args;
	qty rt = sigtype(c, b->sig[0]);

	switch (b->id) {
	case BI_STDC_ROTL: case BI_STDC_ROTR:
		return stdc_rotate(c, b, loc);
	case BI_ELEMENTWISE_ABS:
		return elementwise_abs_call(c, b, loc);
	case BI_ISFPCLASS: case BI_ISSUBNORMAL: case BI_ISZERO: {
		i128 mask = b->id == BI_ISSUBNORMAL ? 144 : 96;
		unsigned nargs = b->id == BI_ISFPCLASS ? 2 : 1;
		uint32_t pre;

		args_list(c, &args);
		if (args.n != nargs) {
			arity(c, loc, nargs, &args);
			return err_node(c, loc);
		}
		if (!is_float(c, N(args.v[0])->ty)) {
			cc_error(c, N(args.v[0])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, N(args.v[0])->ty));
			return err_node(c, loc);
		}
		if (b->id == BI_ISFPCLASS && !const_int(c, args.v[1], &mask)) {
			cc_error(c, loc, "argument to '%s' must be a constant integer", b->name);
			return err_node(c, loc);
		}
		if (mask < 0 || mask > 1023) {
			cc_error(c, loc, "argument value %lld is outside the valid range [0, 1023]", (long long)mask);
			return err_node(c, loc);
		}
		uint32_t a = cplx_reuse(c, rvalue(c, args.v[0]), &pre);
		vec unary;
		vinit(&unary);
		vpush(c, &unary, rvalue(c, a));
		uint32_t sign = mk_builtin(c, BI_SIGNBIT, prim(TY_INT), loc, &unary);
		/* Each pair is the negative/positive membership bit, except NaNs,
		 * whose class is determined by the quiet bit rather than their sign. */
		const int bits[5][2] = {{0,1}, {2,9}, {3,8}, {4,7}, {5,6}};
		uint32_t member[5];
		for (int i = 0; i < 5; i++) {
			uint32_t neg = mk_int(c, !!(mask & ((i128)1 << bits[i][0])), prim(TY_INT), loc);
			uint32_t pos = mk_int(c, !!(mask & ((i128)1 << bits[i][1])), prim(TY_INT), loc);
			uint32_t cond = i ? sign : mk_builtin(c, BI_ISSIGNALING, prim(TY_INT), loc, &unary);
			member[i] = conditional(c, cond, neg, pos, loc);
		}
		uint32_t n = conditional(c, a, member[3], member[4], loc);
		const int ids[] = {BI_ISNAN, BI_ISINF, BI_ISNORMAL};
		for (int i = 2; i >= 0; i--)
			n = conditional(c, mk_builtin(c, ids[i], prim(TY_INT), loc, &unary), member[i], n, loc);
		return with_pre(c, pre, n);
	}
	case BI_FPCLASSIFY: {
		uint32_t saved[6], pre[6];

		args_list(c, &args);
		if (args.n != 6) {
			arity(c, loc, 6, &args);
			return err_node(c, loc);
		}
		if (!is_float(c, N(args.v[5])->ty)) {
			cc_error(c, N(args.v[5])->loc, "floating point classification requires argument of floating point type (passed in '%s')",
			         tyname(c, N(args.v[5])->ty));
			return err_node(c, loc);
		}
		for (int i = 0; i < 6; i++) {
			uint32_t a = rvalue(c, args.v[i]);
			if (i < 5)
				a = convert(c, a, prim(TY_INT));
			saved[i] = cplx_reuse(c, a, &pre[i]);
		}
		/* Match clang's classification tree: save the operand once, test zero,
		 * then NaN and infinity, and finally distinguish normal/subnormal.
		 * A class code is evaluated as its corresponding test is reached. */
		vec unary;
		vinit(&unary);
		vpush(c, &unary, rvalue(c, saved[5]));
		uint32_t n = conditional(c, mk_builtin(c, BI_ISNORMAL, prim(TY_INT), loc, &unary), saved[2], saved[3], loc);
		n = with_pre(c, pre[2], with_pre(c, pre[3], n));
		const int ids[] = {BI_ISNAN, BI_ISINF};
		for (int i = 1; i >= 0; i--)
			n = with_pre(c, pre[i], conditional(c, mk_builtin(c, ids[i], prim(TY_INT), loc, &unary), saved[i], n, loc));
		n = with_pre(c, pre[4], conditional(c, saved[5], n, saved[4], loc));
		return with_pre(c, pre[5], n);
	}
	case BI_CHOOSE: {
		uint32_t cond, x, y;
		i128 v = 0;

		expect(c, T_LPAREN, "'('");
		cond = assign_expr(c);
		expect(c, T_COMMA, "','");
		x = assign_expr(c);
		expect(c, T_COMMA, "','");
		y = assign_expr(c);
		expect_here(c, T_RPAREN, "')'");
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
		expect_here(c, T_RPAREN, "')'");
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
		expect_here(c, T_RPAREN, "')'");
		n = mk(c, N_VAARG, t, loc);
		N(n)->a = ap;
		return n;
	}
	case BI_CONSTANT_P: {
		uint32_t n;

		c->unevaluated++, c->bcp++;	/* (clang: what it names is used) */
		args_list(c, &args);
		c->unevaluated--, c->bcp--;
		if (args.n != 1)
			arity(c, loc, 1, &args);
		for (uint32_t i = 0; i < args.n; i++)
			args.v[i] = decay(c, args.v[i]);
		n = mk_builtin(c, b->id, prim(TY_INT), loc, &args);
		return n;
	}
	case BI_ATOMIC_LOAD_N: case BI_ATOMIC_STORE_N: case BI_ATOMIC_EXCHANGE_N: case BI_ATOMIC_CMPXCHG_N:
	case BI_ATOMIC_LOAD: case BI_ATOMIC_STORE: case BI_ATOMIC_EXCHANGE: case BI_ATOMIC_CMPXCHG:
	case BI_ATOMIC_FETCH_ADD: case BI_ATOMIC_FETCH_SUB: case BI_ATOMIC_FETCH_AND: case BI_ATOMIC_FETCH_OR:
	case BI_ATOMIC_FETCH_XOR: case BI_ATOMIC_ADD_FETCH: case BI_ATOMIC_SUB_FETCH: case BI_ATOMIC_AND_FETCH:
	case BI_ATOMIC_OR_FETCH: case BI_ATOMIC_XOR_FETCH: case BI_ATOMIC_FETCH_NAND: case BI_ATOMIC_FETCH_MAX:
	case BI_ATOMIC_FETCH_MIN: case BI_ATOMIC_NAND_FETCH: case BI_ATOMIC_MAX_FETCH: case BI_ATOMIC_MIN_FETCH:
	case BI_ATOMIC_TEST_AND_SET: case BI_ATOMIC_CLEAR:
	case BI_SYNC_VAL_CAS: case BI_SYNC_BOOL_CAS: case BI_SYNC_LOCK_RELEASE: case BI_SYNC_SYNCHRONIZE:
		return atomic_call(c, b, loc);
	case BI_CONJ: case BI_CREAL: case BI_CIMAG: {
		qty ct = ty_complex(c, sigtype(c, b->sig[0]));
		uint32_t a;

		args_list(c, &args);
		if (args.n != 1) {
			arity(c, loc, 1, &args);
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
	if (b->id == BI_VA_START && !strcmp(b->name, "__builtin_c23_va_start") && args.n > 2) {
		arity(c, loc, 2, &args);
		return err_node(c, loc);
	}
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
			arity(c, loc, np, &args);
		if (b->id == BI_X86_SIMD)
			return x86_simd_call(c, b, &args, rt, loc);
		switch (b->id) {
		case BI_ADD_OVERFLOW: case BI_SUB_OVERFLOW: case BI_MUL_OVERFLOW:
			if (args.n != 3 || !is_ptr(c, N(args.v[2])->ty))
				cc_error(c, args.n == 3 ? N(args.v[2])->loc : loc, "result argument to overflow builtin must be a pointer to a non-const integer type ('%s' invalid)",
					 args.n == 3 ? tyname(c, N(args.v[2])->ty) : "void");
			break;
		case BI_LONGJMP: {
			i128 value;
			if (args.n == 2 && (!const_int(c, args.v[1], &value) || value != 1))
				cc_error(c, N(args.v[1])->loc, "second argument to '__builtin_longjmp' must be the constant integer 1");
			break;
		}
		case BI_ADDC: case BI_SUBC:
			if (args.n != 4 || !is_ptr(c, N(args.v[3])->ty))
				cc_error(c, loc, "carry-out argument to builtin must be a pointer");
			break;
		case BI_ISNAN: case BI_ISINF: case BI_ISFINITE: case BI_SIGNBIT: case BI_ISNORMAL:
		case BI_ISINF_SIGN: case BI_FPCLASSIFY:
			break;
		case BI_ISSIGNALING:
			if (args.n != 1) {
				arity(c, loc, 1, &args);
				return err_node(c, loc);
			}
			if (!is_float(c, N(args.v[0])->ty)) {
				cc_error(c, c->prevloc, "floating point classification requires argument of floating point type (passed in '%s')",
				         tyname(c, N(args.v[0])->ty));
				return err_node(c, loc);
			}
			break;
		}
		if (b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
			/* Fold known strings/payloads; otherwise emit the library call. */
			if (args.n != 1)
				return err_node(c, loc);
			uint32_t n = mk_builtin(c, b->id, rt, loc, &args);
			cval cv;

			if (cc_eval(c, n, &cv) && (cv.kind == CV_INT || cv.kind == CV_FLT))
				return n;
		}
		if (b->id == BI_LIB && !c->o->no_builtin && !lib_builtin_disabled(c->o, "strncmp", 7) &&
		    !strcmp(b->name, "__builtin_strncmp")) {
			uint32_t folded = fold_strncmp_zero(c, &args, loc);
			if (folded)
				return folded;
		}
		if (b->id == BI_LIB) {
			uint32_t result = fp_sign_call(c, b->name + 10, strlen(b->name + 10), ft, &args, loc);
			if (result) return result;
		}
		if (b->id == BI_LIB || b->id == BI_STRLEN || b->id == BI_NAN || b->id == BI_NANF || b->id == BI_NANS) {
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
