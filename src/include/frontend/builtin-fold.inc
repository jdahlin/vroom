/* The builtins a constant expression folds, besides integer-builtins.inc's
 * (which every front end folds): BUILTIN_FOLD (spelling, operation,
 * languages).  See builtin-fold.h for how it is read.
 *
 * operation   the vfold_op the spelling is an instance of
 * languages   VFOLD_C, VFOLD_CXX or VFOLD_BOTH: where it folds.  A bit is
 *             set when that front end evaluates the builtin, and clear
 *             when it does not (yet): a spelling not listed folds nowhere,
 *             and __has_constexpr_builtin says so.
 *
 * A name where clang's answer differs is the exception that is stated
 * with it.
 */

/* the integer operations on more spellings than integer-builtins.inc has */
BUILTIN_FOLD("__builtin_ffs", ffs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_ffsl", ffs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_ffsll", ffs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_abs", absolute, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_labs", absolute, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_llabs", absolute, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_expect", expect, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_expect_with_probability", expect, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_bswapg", bswap_generic, VFOLD_CXX)
BUILTIN_FOLD("__builtin_launder", identity, VFOLD_CXX)

/* the type-generic counts */
BUILTIN_FOLD("__builtin_clzg", clz_generic, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_ctzg", ctz_generic, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_popcountg", popcount_generic, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_stdc_count_ones", stdc_count_ones, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_stdc_trailing_zeros", stdc_trailing_zeros, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_stdc_rotate_left", stdc_rotate_left, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_stdc_rotate_right", stdc_rotate_right, VFOLD_BOTH)

/* AArch64's spellings of integer operations (target-builtins.inc maps them
   to the C front end's nodes of the same operation) */
BUILTIN_FOLD("__builtin_arm_clz", clz, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_arm_clz64", clz, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_arm_cls", clrsb, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_arm_cls64", clrsb, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_arm_rbit", bitreverse, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_arm_rbit64", bitreverse, VFOLD_BOTH)

/* queries */
BUILTIN_FOLD("__builtin_constant_p", constant_p, VFOLD_BOTH)
BUILTIN_FOLD("__atomic_always_lock_free", lock_free, VFOLD_BOTH)
BUILTIN_FOLD("__atomic_is_lock_free", lock_free, VFOLD_BOTH)
BUILTIN_FOLD("__c11_atomic_is_lock_free", lock_free, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_classify_type", classify_type, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_object_size", object_size, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_dynamic_object_size", object_size, VFOLD_BOTH)

/* floating values: the sign operations, the constants, the classes and
   the comparisons of <math.h> */
BUILTIN_FOLD("__builtin_fabs", fabs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_fabsf", fabs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_fabsl", fabs, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_copysign", copysign, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_copysignf", copysign, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_copysignl", copysign, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_huge_val", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_huge_valf", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_huge_vall", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_inf", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_inff", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_infl", huge_val, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nan", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nanf", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nanl", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nans", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nansf", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_nansl", nan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isnan", isnan, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isinf", isinf, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isinf_sign", isinf_sign, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isfinite", isfinite, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isnormal", isnormal, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_signbit", signbit, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_fpclassify", fpclassify, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isfpclass", isfpclass, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_iszero", iszero, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_issubnormal", issubnormal, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_issignaling", issignaling, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isgreater", isgreater, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isgreaterequal", isgreaterequal, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isless", isless, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_islessequal", islessequal, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_islessgreater", islessgreater, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_isunordered", isunordered, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_cabs", cabs, VFOLD_CXX)
BUILTIN_FOLD("__builtin_cabsf", cabs, VFOLD_CXX)
BUILTIN_FOLD("__builtin_cabsl", cabs, VFOLD_CXX)
BUILTIN_FOLD("__builtin_carg", carg, VFOLD_CXX)
BUILTIN_FOLD("__builtin_cargf", carg, VFOLD_CXX)
BUILTIN_FOLD("__builtin_cargl", carg, VFOLD_CXX)
/* AArch64's scalar half-precision add and subtract fold as the + and - they
   lower to */
BUILTIN_FOLD("__builtin_neon_vaddh_f16", fp16_arithmetic, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_neon_vsubh_f16", fp16_arithmetic, VFOLD_BOTH)

/* the string and memory functions on arrays of constants */
BUILTIN_FOLD("__builtin_strlen", strlen, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_strcmp", strcmp, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_strncmp", strncmp, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_memcmp", memcmp, VFOLD_BOTH)
BUILTIN_FOLD("__builtin_memchr", memchr, VFOLD_CXX)
BUILTIN_FOLD("__builtin_char_memchr", memchr, VFOLD_CXX)
BUILTIN_FOLD("__builtin_wcslen", wcslen, VFOLD_CXX)
BUILTIN_FOLD("__builtin_wmemcmp", wmemcmp, VFOLD_CXX)
BUILTIN_FOLD("__builtin_wmemchr", wmemchr, VFOLD_CXX)

/* the overflow checks (they write the object the third argument points to)
   and x86's add with carry */
BUILTIN_FOLD("__builtin_add_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_sub_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_mul_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_sadd_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_saddl_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_saddll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_uadd_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_uaddl_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_uaddll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_ssub_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_ssubl_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_ssubll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_usub_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_usubl_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_usubll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_smul_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_smull_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_smulll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_umul_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_umull_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_umulll_overflow", overflow, VFOLD_CXX)
BUILTIN_FOLD("__builtin_ia32_addcarryx_u64", add_carry, VFOLD_CXX)
