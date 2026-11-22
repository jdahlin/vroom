/*
 * builtin-registry.cpp: [ext:gnu_library_builtins] the registry of the
 * compiler builtins: one row for each builtin whose call is not a call of
 * one function type with the standard conversions.  A name of the registry
 * says how its call is checked and typed (builtin_form), and whether and
 * how a constant expression evaluates it (builtin_fold); a name of the C
 * front end's builtin table (frontend/c/builtin-table.cpp) that is not here
 * is a function of the prototype that table gives it
 * (library-builtins.cpp, compiler_builtin).  The registry is the one place
 * the names of these special cases are spelled: analysis, constant
 * evaluation and lowering look a builtin up by the row its entity holds
 * (entity::gnu_builtin_row).
 */
#include "../../cplusplus-internal.h"
#include "frontend/c/ast-builder.h"
#include "frontend/cplusplus/builtin-registry.h"

namespace cplusplus
{

#define ROW(n, f, w, k)                                                        \
    {n,       builtin_form::f, w, builtin_fold::k, builtin_lowering::node,     \
     nullptr, nullptr}
/* a builtin of the C++ front end alone: its lowering, prototype and library
   function */
#define ROWL(n, f, w, k, l, p, s)                                              \
    {n, builtin_form::f, w, builtin_fold::k, builtin_lowering::l, p, s}

/* the rows, in no order: gnu_builtin_find searches them */
static const gnu_builtin builtin_registry[] = {
    /* the atomic builtins: any name ast_builder_atomic_shape knows, found by
       gnu_builtin_find (one row stands for all) */
    ROW("__atomic_", atomic, 0, none),
    /* a builtin of the C front end whose signature this front end has no
       types for (gnu_builtin_unsupported_row) */
    ROW("", unsupported, 0, none),
    /* the builtins of the C front end's table that are calls of the C
       library function (library-builtins.cpp, library_function_builtin) */
    /* clang's builtins of no C front end node */
    ROWL("__builtin_verbose_trap", signature, 0, none, trap, "vSS", nullptr),
    ROWL("__builtin_assume_dereferenceable", signature, 0, none, discard, "vPz",
         nullptr),
    ROWL("__builtin_memcpy_inline", library_alias, 0, none, node, "vpPz",
         "memcpy"),
    ROWL("__builtin_char_memchr", library_alias, 4, string, node, "SSiz",
         "memchr"),
    ROWL("__builtin_launder", launder, 0, identity, identity, nullptr, nullptr),
    ROWL("__builtin_clear_padding", clear_padding, 0, none, clear_padding,
         nullptr, nullptr),
    ROW("__builtin_addcb", carry, 0, none),
    ROW("__builtin_addcs", carry, 0, none),
    ROW("__builtin_addc", carry, 0, none),
    ROW("__builtin_addcl", carry, 0, none),
    ROW("__builtin_addcll", carry, 0, none),
    ROW("__builtin_subcb", carry, 0, none),
    ROW("__builtin_subcs", carry, 0, none),
    ROW("__builtin_subc", carry, 0, none),
    ROW("__builtin_subcl", carry, 0, none),
    ROW("__builtin_subcll", carry, 0, none),
    ROW("__builtin_complex", complex, 0, none),
    ROW("__builtin_creal", complex_part, 0, none),
    ROW("__builtin_crealf", complex_part, 0, none),
    ROW("__builtin_creall", complex_part, 0, none),
    ROW("__builtin_cimag", complex_part, 0, none),
    ROW("__builtin_cimagf", complex_part, 0, none),
    ROW("__builtin_cimagl", complex_part, 0, none),
    ROW("__builtin_conj", complex_conjugate, 0, none),
    ROW("__builtin_conjf", complex_conjugate, 0, none),
    ROW("__builtin_conjl", complex_conjugate, 0, none),
    ROWL("__builtin_invoke", invoke, 0, none, node, nullptr, nullptr),
    ROWL("__builtin_bswapg", bswapg, 0, bswap, bswap, nullptr, nullptr),
    /* the builtins a constant expression folds: the integer operations,
       the floating ones, the string functions; a `library` row is a call
       of the C library function (library_function_builtin) */
    ROW("__builtin_clz", signature, 0, integer),
    ROW("__builtin_clzl", signature, 0, integer),
    ROW("__builtin_clzll", signature, 0, integer),
    ROW("__builtin_ctz", signature, 1, integer),
    ROW("__builtin_ctzl", signature, 1, integer),
    ROW("__builtin_ctzll", signature, 1, integer),
    ROW("__builtin_popcount", signature, 2, integer),
    ROW("__builtin_popcountl", signature, 2, integer),
    ROW("__builtin_popcountll", signature, 2, integer),
    ROW("__builtin_parity", signature, 4, integer),
    ROW("__builtin_parityl", signature, 4, integer),
    ROW("__builtin_parityll", signature, 4, integer),
    ROW("__builtin_clrsb", signature, 5, integer),
    ROW("__builtin_clrsbl", signature, 5, integer),
    ROW("__builtin_clrsbll", signature, 5, integer),
    ROW("__builtin_ffsl", signature, 3, integer),
    ROW("__builtin_ffsll", signature, 3, integer),
    ROW("__builtin_ffs", library, 3, integer),
    ROW("__builtin_bswap16", signature, 6, integer),
    ROW("__builtin_bswap32", signature, 6, integer),
    ROW("__builtin_bswap64", signature, 6, integer),
    ROW("__builtin_bitreverse8", signature, 7, integer),
    ROW("__builtin_bitreverse16", signature, 7, integer),
    ROW("__builtin_bitreverse32", signature, 7, integer),
    ROW("__builtin_bitreverse64", signature, 7, integer),
    ROW("__builtin_rotateleft8", signature, 8, integer),
    ROW("__builtin_rotateleft16", signature, 8, integer),
    ROW("__builtin_rotateleft32", signature, 8, integer),
    ROW("__builtin_rotateleft64", signature, 8, integer),
    ROW("__builtin_rotateright8", signature, 9, integer),
    ROW("__builtin_rotateright16", signature, 9, integer),
    ROW("__builtin_rotateright32", signature, 9, integer),
    ROW("__builtin_rotateright64", signature, 9, integer),
    ROW("__builtin_expect", signature, 11, integer),
    ROW("__builtin_expect_with_probability", signature, 11, integer),
    ROW("__builtin_abs", library, 10, integer),
    ROW("__builtin_labs", library, 10, integer),
    ROW("__builtin_llabs", library, 10, integer),
    ROW("__builtin_fabs", library, 13, floating),
    ROW("__builtin_fabsf", library, 13, floating),
    ROW("__builtin_fabsl", library, 13, floating),
    ROW("__builtin_copysign", library, 14, floating),
    ROW("__builtin_copysignf", library, 14, floating),
    ROW("__builtin_copysignl", library, 14, floating),
    ROW("__builtin_strlen", library, 0, string),
    ROW("__builtin_strcmp", library, 1, string),
    ROW("__builtin_strncmp", library, 2, string),
    ROW("__builtin_memcmp", library, 3, string),
    ROW("__builtin_memchr", library, 4, string),
    ROW("__builtin_wcslen", library, 5, string),
    ROW("__builtin_wmemcmp", library, 6, string),
    ROW("__builtin_wmemchr", library, 7, string),
    /* int __builtin_constant_p(...), and the lock-free queries */
    ROW("__builtin_constant_p", query, 1, constant_p),
    ROW("__atomic_always_lock_free", query, 2, lock_free),
    ROW("__atomic_is_lock_free", query, 3, lock_free),
    ROW("__c11_atomic_is_lock_free", query, 4, lock_free),
    /* the va_list builtins: start, c23 start, end, copy, Microsoft's */
    ROW("__builtin_va_start", va_list, 1, none),
    ROW("__builtin_c23_va_start", va_list, 2, none),
    ROW("__builtin_va_end", va_list, 3, none),
    ROW("__builtin_va_copy", va_list, 4, none),
    ROW("__builtin_ms_va_start", va_list, 5, none),
    ROW("__builtin_ms_va_end", va_list, 5, none),
    ROW("__builtin_ms_va_copy", va_list, 5, none),
    /* the overflow builtins and their typed forms */
    ROW("__builtin_add_overflow", overflow, 1, overflow),
    ROW("__builtin_sub_overflow", overflow, 2, overflow),
    ROW("__builtin_mul_overflow", overflow, 3, overflow),
    ROW("__builtin_sadd_overflow", overflow, 1, overflow),
    ROW("__builtin_saddl_overflow", overflow, 1, overflow),
    ROW("__builtin_saddll_overflow", overflow, 1, overflow),
    ROW("__builtin_uadd_overflow", overflow, 1, overflow),
    ROW("__builtin_uaddl_overflow", overflow, 1, overflow),
    ROW("__builtin_uaddll_overflow", overflow, 1, overflow),
    ROW("__builtin_ssub_overflow", overflow, 2, overflow),
    ROW("__builtin_ssubl_overflow", overflow, 2, overflow),
    ROW("__builtin_ssubll_overflow", overflow, 2, overflow),
    ROW("__builtin_usub_overflow", overflow, 2, overflow),
    ROW("__builtin_usubl_overflow", overflow, 2, overflow),
    ROW("__builtin_usubll_overflow", overflow, 2, overflow),
    ROW("__builtin_smul_overflow", overflow, 3, overflow),
    ROW("__builtin_smull_overflow", overflow, 3, overflow),
    ROW("__builtin_smulll_overflow", overflow, 3, overflow),
    ROW("__builtin_umul_overflow", overflow, 3, overflow),
    ROW("__builtin_umull_overflow", overflow, 3, overflow),
    ROW("__builtin_umulll_overflow", overflow, 3, overflow),
    /* the type-generic bit counts: popcountg, clzg, ctzg */
    ROW("__builtin_popcountg", bit_count, 1, bit_count),
    ROW("__builtin_clzg", bit_count, 2, bit_count),
    ROW("__builtin_ctzg", bit_count, 3, bit_count),
    /* the type-generic classifications and comparisons of <math.h> (C17
       7.12.3, 7.12.14): the arguments as they are, unpromoted */
    ROW("__builtin_fpclassify", generic, 6, floating),
    ROW("__builtin_isfinite", generic, 2, floating),
    ROW("__builtin_isgreater", generic, 7, floating),
    ROW("__builtin_isgreaterequal", generic, 8, floating),
    ROW("__builtin_isinf", generic, 1, floating),
    ROW("__builtin_isinf_sign", generic, 5, floating),
    ROW("__builtin_isless", generic, 9, floating),
    ROW("__builtin_islessequal", generic, 10, floating),
    ROW("__builtin_islessgreater", generic, 11, floating),
    ROW("__builtin_isnan", generic, 0, floating),
    ROW("__builtin_isnormal", generic, 3, floating),
    ROW("__builtin_issignaling", generic, 0, floating_constant),
    ROW("__builtin_isunordered", generic, 12, floating),
    ROW("__builtin_signbit", generic, 4, floating),
    /* the floating constants: infinity and quiet or signalling NaNs */
    ROW("__builtin_huge_val", floating_constant, 0, floating_constant),
    ROW("__builtin_huge_valf", floating_constant, 0, floating_constant),
    ROW("__builtin_huge_vall", floating_constant, 0, floating_constant),
    ROW("__builtin_inf", floating_constant, 0, floating_constant),
    ROW("__builtin_inff", floating_constant, 0, floating_constant),
    ROW("__builtin_infl", floating_constant, 0, floating_constant),
    ROW("__builtin_nan", floating_constant, 0, floating_constant),
    ROW("__builtin_nanf", floating_constant, 0, floating_constant),
    ROW("__builtin_nanl", floating_constant, 0, floating_constant),
    ROW("__builtin_nans", floating_constant, 0, floating_constant),
    ROW("__builtin_nansf", floating_constant, 0, floating_constant),
    ROW("__builtin_nansl", floating_constant, 0, floating_constant),
};

#undef ROW
#undef ROWL

const gnu_builtin *gnu_builtin_find(const char *spelling, size_t length,
                                    uint16_t *row)
{
    const size_t count = sizeof builtin_registry / sizeof builtin_registry[0];
    /* (row 0 is the atomic builtins' own) */
    int written = 0, shape = 0;
    for (size_t i = 2; i < count; i++) {
        const gnu_builtin &b = builtin_registry[i];
        if (strlen(b.name) == length && !memcmp(b.name, spelling, length)) {
            *row = static_cast<uint16_t>(i + 1);
            return &b;
        }
    }
    if (ast_builder_atomic_shape(spelling, length, &written, &shape)) {
        *row = 1;
        return &builtin_registry[0];
    }
    return nullptr;
}

const gnu_builtin &gnu_builtin_at(uint16_t row)
{
    return builtin_registry[row - 1];
}

bool gnu_builtin_untyped(uint16_t row)
{
    switch (gnu_builtin_at(row).form) {
    case builtin_form::query:
    case builtin_form::va_list:
    case builtin_form::overflow:
    case builtin_form::atomic:
    case builtin_form::complex:
    case builtin_form::launder:
    case builtin_form::clear_padding:
    case builtin_form::bswapg:
    case builtin_form::invoke:
    case builtin_form::unsupported:
        return true;
    default:
        return false;
    }
}

} // namespace cplusplus
