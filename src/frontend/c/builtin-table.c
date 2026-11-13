/* vcc front end: the builtin table (name, id, signature), its name directory and the library-signature decoder. A new builtin's other facts: its BI_ id in ccint.h, its row in builtin-table.c, its typing in builtin-call.c, its folding in builtin-fold.c, and its code in the three generator switches (gen_builtin in arch/a64/codegen/builtin.c, wk_builtin in codegen/walk/expr.c, wbuiltin in arch/wasm/codegen/wcall.c) */
#include "cc-internal.h"

/* "too few/many arguments", as clang: the caret after the arguments, the callee's name underlined */
CC_PRIVATE void cc_builtin_arity_check(cc *c, uint32_t loc, unsigned want, const cc_id_list *args)
{
	diag_span r = { loc, pp_token_end_find(c->p, loc) };
	unsigned have = args->n;

	if (have && AST_NODE_IS_ERROR(args->v[have - 1]))	/* the ')' was missing (cc_builtin_arguments_parse): said */
		return;

	report(c, "error", c->prevloc, &r, 1, NULL, 0, "too %s arguments to function call, expected %u, have %u",
	       have < want ? "few" : "many", want, have);
}

static const cc_builtin cc_builtin_table[] = {
	{ "__builtin_ia32_pblendvb128", BI_X86_BLENDVB, 1, "BBBB" },
	{ "__builtin_elementwise_abs", BI_ELEMENTWISE_ABS, 1, "i." },
	{ "__builtin_ia32_movmskps", BI_X86_SIMD, 1, "iF" },
	{ "__builtin_ia32_shufps", BI_X86_SIMD, 1, "FFFi" },
	{ "__builtin_ia32_maxps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_minps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_cmpnleps", BI_X86_SIMD, 1, "FFF" },
	{ "__builtin_ia32_psllwi128", BI_X86_SIMD, 1, "HHi" },
	{ "__builtin_ia32_psrlwi128", BI_X86_SIMD, 1, "HHi" },
	{ "__builtin_ia32_pcmpistri128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpistrm128", BI_X86_PCMPSTR, 1, "BBBc" },
	{ "__builtin_ia32_pcmpistria128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpistric128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpistrio128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpistris128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpistriz128", BI_X86_PCMPSTR, 1, "iBBc" },
	{ "__builtin_ia32_pcmpestri128", BI_X86_PCMPSTR, 1, "iBiBic" },
	{ "__builtin_ia32_pcmpestrm128", BI_X86_PCMPSTR, 1, "BBiBic" },
	{ "__builtin_ia32_pcmpestria128", BI_X86_PCMPSTR, 1, "iBiBic" },
	{ "__builtin_ia32_pcmpestric128", BI_X86_PCMPSTR, 1, "iBiBic" },
	{ "__builtin_ia32_pcmpestrio128", BI_X86_PCMPSTR, 1, "iBiBic" },
	{ "__builtin_ia32_pcmpestris128", BI_X86_PCMPSTR, 1, "iBiBic" },
	{ "__builtin_ia32_pcmpestriz128", BI_X86_PCMPSTR, 1, "iBiBic" },
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
	{ "__builtin_flt_rounds", BI_FLT_ROUNDS, 0, "i" },
	{ "__builtin_os_log_format", BI_OS_LOG_FORMAT, 0, "ppS." },
	{ "__builtin_os_log_format_buffer_size", BI_OS_LOG_SIZE, 0, "zS." },
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
	/* legacy __sync_*: the __atomic_* forms with seq_cst (sig "s.": cc_builtin_atomic_build appends the order) */
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
#define CC_BUILTIN_SYNC_SIZES(n, bi) { n "_1", bi, 0, "s." }, { n "_2", bi, 0, "s." }, { n "_4", bi, 0, "s." }, { n "_8", bi, 0, "s." }, \
	{ n "_16", bi, 0, "s." }
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_add", BI_ATOMIC_FETCH_ADD),
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_sub", BI_ATOMIC_FETCH_SUB),
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_and", BI_ATOMIC_FETCH_AND),
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_or", BI_ATOMIC_FETCH_OR),
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_xor", BI_ATOMIC_FETCH_XOR),
	CC_BUILTIN_SYNC_SIZES("__sync_add_and_fetch", BI_ATOMIC_ADD_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_sub_and_fetch", BI_ATOMIC_SUB_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_and_and_fetch", BI_ATOMIC_AND_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_or_and_fetch", BI_ATOMIC_OR_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_xor_and_fetch", BI_ATOMIC_XOR_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_fetch_and_nand", BI_ATOMIC_FETCH_NAND),
	CC_BUILTIN_SYNC_SIZES("__sync_nand_and_fetch", BI_ATOMIC_NAND_FETCH),
	CC_BUILTIN_SYNC_SIZES("__sync_lock_test_and_set", BI_ATOMIC_EXCHANGE_N),
	CC_BUILTIN_SYNC_SIZES("__sync_val_compare_and_swap", BI_SYNC_VAL_CAS),
	CC_BUILTIN_SYNC_SIZES("__sync_bool_compare_and_swap", BI_SYNC_BOOL_CAS),
	CC_BUILTIN_SYNC_SIZES("__sync_lock_release", BI_SYNC_LOCK_RELEASE),
	CC_BUILTIN_SYNC_SIZES("__sync_swap", BI_ATOMIC_EXCHANGE_N),
#undef CC_BUILTIN_SYNC_SIZES
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
	{ "__builtin_memcpy", BI_LIB, (CC_LIB_MEMCPY << 1) | 0, "ppPz" }, { "__builtin_memmove", BI_LIB, (CC_LIB_MEMMOVE << 1) | 0, "ppPz" },
	{ "__builtin_memset", BI_LIB, (CC_LIB_MEMSET << 1) | 0, "ppiz" }, { "__builtin_memcmp", BI_LIB, 0, "iPPz" },
	{ "__builtin_memchr", BI_LIB, 0, "pPiz" }, { "__builtin_bzero", BI_LIB, 0, "vpz" },
	{ "__builtin_strcmp", BI_LIB, (CC_LIB_STRCMP << 1) | 0, "iSS" }, { "__builtin_strncmp", BI_LIB, 0, "iSSz" },
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
	{ "__builtin_complex", BI_COMPLEX, 1, "i." },
	{ "__builtin_conj", BI_CONJ, 0, "d." }, { "__builtin_conjf", BI_CONJ, 0, "f." }, { "__builtin_conjl", BI_CONJ, 0, "D." },
	{ "__builtin_creal", BI_CREAL, 0, "d." }, { "__builtin_crealf", BI_CREAL, 0, "f." }, { "__builtin_creall", BI_CREAL, 0, "D." },
	{ "__builtin_cimag", BI_CIMAG, 0, "d." }, { "__builtin_cimagf", BI_CIMAG, 0, "f." }, { "__builtin_cimagl", BI_CIMAG, 0, "D." },
	{ "__builtin_fabs", BI_LIB, 1, "dd" }, { "__builtin_fabsf", BI_LIB, 1, "ff" }, { "__builtin_fabsl", BI_LIB, (CC_LIB_FABSL << 1) | 1, "DD" },
	{ "__builtin_copysign", BI_LIB, 1, "ddd" }, { "__builtin_copysignf", BI_LIB, 1, "fff" },
	{ "__builtin_copysignl", BI_LIB, 1, "DDD" },
	{ "__builtin_pow", BI_LIB, 1, "ddd" },
	{ "__builtin_log2", BI_LIB, 1, "dd" },
	{ "__builtin_sqrt", BI_LIB, 0, "dd" }, { "__builtin_sqrtf", BI_LIB, 0, "ff" },
	{ "__builtin_floor", BI_LIB, 0, "dd" }, { "__builtin_ceil", BI_LIB, 0, "dd" },
	{ "__builtin_fmax", BI_LIB, 0, "ddd" }, { "__builtin_fmin", BI_LIB, 0, "ddd" },
	{ "__builtin_ldexp", BI_LIB, 0, "ddi" }, { "__builtin_ldexpf", BI_LIB, 0, "ffi" },
	{ "__builtin_ldexpl", BI_LIB, 0, "DDi" }, { "__builtin_scalbn", BI_LIB, 0, "ddi" },
	{ "__builtin_floorf", BI_LIB, 0, "ff" }, { "__builtin_ceilf", BI_LIB, 0, "ff" },
	{ "__builtin_trunc", BI_LIB, 0, "dd" }, { "__builtin_truncf", BI_LIB, 0, "ff" },
	{ "__builtin_round", BI_LIB, 0, "dd" }, { "__builtin_roundf", BI_LIB, 0, "ff" },
	{ "__builtin_fmaxf", BI_LIB, 0, "fff" }, { "__builtin_fminf", BI_LIB, 0, "fff" },
	{ "__builtin___memcpy_chk", BI_LIB, CC_LIB_CHK << 1, "ppPzz" }, { "__builtin___memmove_chk", BI_LIB, CC_LIB_CHK << 1, "ppPzz" },
	{ "__builtin___memset_chk", BI_LIB, CC_LIB_CHK << 1, "ppizz" }, { "__builtin___strcpy_chk", BI_LIB, CC_LIB_CHK << 1, "ssSz" },
	{ "__builtin___stpcpy_chk", BI_LIB, 0, "ssSz" }, { "__builtin___strcat_chk", BI_LIB, CC_LIB_CHK << 1, "ssSz" },
	{ "__builtin___strncpy_chk", BI_LIB, CC_LIB_CHK << 1, "ssSzz" }, { "__builtin___strncat_chk", BI_LIB, CC_LIB_CHK << 1, "ssSzz" },
	{ "__builtin___stpncpy_chk", BI_LIB, 0, "ssSzz" }, { "__builtin___strlcpy_chk", BI_LIB, 0, "zsSzz" },
	{ "__builtin___strlcat_chk", BI_LIB, 0, "zsSzz" }, { "__builtin___memccpy_chk", BI_LIB, 0, "ppPizz" },
	{ "__builtin___sprintf_chk", BI_LIB, 0, "isizS." }, { "__builtin___snprintf_chk", BI_LIB, 0, "iszizS." },
	{ "__builtin___vsprintf_chk", BI_LIB, 0, "isizSV" }, { "__builtin___vsnprintf_chk", BI_LIB, 0, "iszizSV" },
	{ NULL, 0, 0, NULL }
};

/* name of a library builtin (sym val of an S_BUILTIN) */
CC_PRIVATE const cc_builtin *cc_builtin_table_get(void) { return cc_builtin_table; }
const char *cc_builtin_name(int64_t i) { return cc_builtin_table[i].name; }
int cc_builtin_libkind(int64_t i) { return cc_builtin_table[i].flags >> 1; }

/* A fortified call (CC_LIB_CHK builtin bi, na arguments, the object size last, the length before it) whose
 * check cannot fail: the object size is unknown ((size_t)-1) or a constant length is within a constant
 * size.  Then the plain builtin's index (call it without the last argument), else -1.  Clang's fortify
 * simplification; its warnings come from the front end. */
int64_t cc_chk_simplify(int64_t bi, int na, bool osz_const, uint64_t osz, bool len_const, uint64_t len)
{
	int64_t p = cc_builtin_plain(bi);

	if (p < 0 || na < 3 || !osz_const)
		return -1;
	if (osz == ~(uint64_t)0 || (na >= 4 && len_const && len <= osz))
		return p;
	return -1;
}

/* __builtin___X_chk -> __builtin_X (fortified call with the object size dropped), -1: none */
int64_t cc_builtin_plain(int64_t i)
{
	const char *n = cc_builtin_table[i].name;
	size_t len = strlen(n);
	char b[64];

	if (strncmp(n, "__builtin___", 12) || len < 16 || len - 4 >= sizeof b || strcmp(n + len - 4, "_chk"))
		return -1;
	memcpy(b, "__builtin_", 10);
	memcpy(b + 10, n + 12, len - 16);
	b[len - 6] = 0;
	for (int64_t j = 0; cc_builtin_table[j].name; j++)
		if (cc_builtin_table[j].id == BI_LIB && !strcmp(cc_builtin_table[j].name, b))
			return j;
	return -1;
}

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
	for (uint32_t i = 0; cc_builtin_table[i].name; i++)
		if (cc_builtin_table[i].id == BI_LIB && !strcmp(cc_builtin_table[i].name, b)) {
			*clang_enc = 0;
			return cc_builtin_table[i].sig;
		}
	*clang_enc = 1;
	return cc_library_builtin_signature(b);
}

CC_PRIVATE qty cc_builtin_signature_type(cc *c, char ch)
{
	switch (ch) {
	case 'v': return cc_type_basic(TY_VOID);
	case 'b': return cc_type_basic(TY_BOOL);
	case 'C': return cc_type_basic(TY_UCHAR);
	case 'h': return cc_type_basic(TY_USHORT);
	case 'i': return cc_type_basic(TY_INT);
	case 'u': return cc_type_basic(TY_UINT);
	case 'l': return cc_type_basic(TY_LONG);
	case 'L': return cc_type_basic(TY_ULONG);
	case 'w': return cc_type_basic(TY_LLONG);
	case 'W': return cc_type_basic(TY_ULLONG);
	case 'z': return c->t_size;
	case 'd': return cc_type_basic(TY_DOUBLE);
	case 'f': return cc_type_basic(TY_FLOAT);
	case 'F': return cc_type_vector(c, cc_type_basic(TY_FLOAT), 16);
	case 'c': return cc_type_basic(TY_CHAR);
	case 'B': return cc_type_vector(c, cc_type_basic(TY_CHAR), 16);
	case 'H': return cc_type_vector(c, cc_type_basic(TY_SHORT), 16);
	case 'D': return cc_type_basic(TY_LDOUBLE);
	case 'p': return cc_type_pointer(c, cc_type_basic(TY_VOID));
	case 'P': return cc_type_pointer(c, QT(TY_VOID, Q_CONST));
	case 's': return cc_type_pointer(c, cc_type_basic(TY_CHAR));
	case 'S': return cc_type_pointer(c, QT(TY_CHAR, Q_CONST));
	case 'V':	/* a parameter: x86-64's array type is adjusted to the element pointer */
		return AST_TYPE_KIND(c, c->t_valist) == TY_ARRAY ? cc_type_pointer(c, AST_TYPE(c, c->t_valist)->base) : c->t_valist;
	default: abort();
	}
}

/* the function type of a signature in vcc's letters (sigtype) */
CC_PRIVATE qty cc_builtin_signature_function_type(cc *c, const char *sig)
{
	qty par[16];
	uint32_t n = 0;
	int fl = 0;

	for (const char *s = sig + 1; *s; s++) {
		if (*s == '.') {
			fl |= TF_VARIADIC;
			break;
		}
		par[n++] = cc_builtin_signature_type(c, *s);
	}
	return cc_type_function(c, cc_builtin_signature_type(c, sig[0]), par, n, fl);
}

CC_PRIVATE qty cc_builtin_type(cc *c, const cc_builtin *b)
{
	return cc_builtin_signature_function_type(c, b->sig);
}

/* ---------------- expressions: builtins ---------------- */

static uint32_t *cc_builtin_directory[NDIR];

/* __has_builtin for the names the front end implements (the pp asks through pp_builtin_known) */
static int cc_builtin_is_known(const char *n)
{
	for (uint32_t i = 0; cc_builtin_table[i].name; i++)
		if (!strcmp(cc_builtin_table[i].name, n))
			return cc_builtin_table[i].id != BI_IS_CONSTANT_EVALUATED;	/* clang: C++ only */
	return cc_library_builtin_is_known(n);
}

__attribute__((constructor)) static void cc_builtin_hook_register(void)
{
	pp_builtin_known = cc_builtin_is_known;
}

CC_PRIVATE void cc_builtin_table_init(void)
{
	static bool done;

	if (__atomic_load_n(&done, __ATOMIC_ACQUIRE))
		return;
	/* the table is shared by all TUs; building it twice is harmless */
	for (uint32_t i = 0; cc_builtin_table[i].name; i++) {
		size_t l = strlen(cc_builtin_table[i].name);
		char *s = malloc(l + 17);
		uint32_t id;

		memcpy(s, cc_builtin_table[i].name, l);
		memset(s + l, 0, 17);
		id = vintern_copy(s, l);
		if (!cc_builtin_directory[id >> DIR_BITS]) {
			uint32_t *pg = calloc(DIR_PAGE, sizeof(uint32_t));

			cc_builtin_directory[id >> DIR_BITS] = pg;
		}
		cc_builtin_directory[id >> DIR_BITS][id & (DIR_PAGE - 1)] = i + 1;
		free(s);
	}
	__atomic_store_n(&done, true, __ATOMIC_RELEASE);
}

CC_PRIVATE const cc_builtin *cc_builtin_lookup(uint32_t id)
{
	uint32_t *pg = cc_builtin_directory[id >> DIR_BITS];
	uint32_t i = pg ? pg[id & (DIR_PAGE - 1)] : 0;

	return i ? &cc_builtin_table[i - 1] : NULL;
}

bool cc_eval(cc *c, uint32_t n, cval *v) { return cc_constant_eval(c, n, v); }
bool cc_is_signed(cc *c, qty t) { return cc_type_is_signed(c, t); }
