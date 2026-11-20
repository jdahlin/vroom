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
    ROW("__builtin_strlen", library, 0, none),
    ROW("__builtin_ffs", library, 0, none),
    /* clang's builtins of no C front end node */
    ROWL("__builtin_verbose_trap", signature, 0, none, trap, "vSS", nullptr),
    ROWL("__builtin_assume_dereferenceable", signature, 0, none, discard, "vPz",
         nullptr),
    ROWL("__builtin_memcpy_inline", library_alias, 0, none, node, "vpPz",
         "memcpy"),
    ROWL("__builtin_char_memchr", library_alias, 0, none, node, "SSiz",
         "memchr"),
    ROWL("__builtin_launder", launder, 0, identity, identity, nullptr, nullptr),
    ROWL("__builtin_bswapg", bswapg, 0, bswap, bswap, nullptr, nullptr),
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
    ROW("__builtin_add_overflow", overflow, 0, overflow),
    ROW("__builtin_sub_overflow", overflow, 0, overflow),
    ROW("__builtin_mul_overflow", overflow, 0, overflow),
    ROW("__builtin_sadd_overflow", overflow, 0, overflow),
    ROW("__builtin_saddl_overflow", overflow, 0, overflow),
    ROW("__builtin_saddll_overflow", overflow, 0, overflow),
    ROW("__builtin_uadd_overflow", overflow, 0, overflow),
    ROW("__builtin_uaddl_overflow", overflow, 0, overflow),
    ROW("__builtin_uaddll_overflow", overflow, 0, overflow),
    ROW("__builtin_ssub_overflow", overflow, 0, overflow),
    ROW("__builtin_ssubl_overflow", overflow, 0, overflow),
    ROW("__builtin_ssubll_overflow", overflow, 0, overflow),
    ROW("__builtin_usub_overflow", overflow, 0, overflow),
    ROW("__builtin_usubl_overflow", overflow, 0, overflow),
    ROW("__builtin_usubll_overflow", overflow, 0, overflow),
    ROW("__builtin_smul_overflow", overflow, 0, overflow),
    ROW("__builtin_smull_overflow", overflow, 0, overflow),
    ROW("__builtin_smulll_overflow", overflow, 0, overflow),
    ROW("__builtin_umul_overflow", overflow, 0, overflow),
    ROW("__builtin_umull_overflow", overflow, 0, overflow),
    ROW("__builtin_umulll_overflow", overflow, 0, overflow),
    /* the type-generic bit counts: popcountg, clzg, ctzg */
    ROW("__builtin_popcountg", bit_count, 1, bit_count),
    ROW("__builtin_clzg", bit_count, 2, bit_count),
    ROW("__builtin_ctzg", bit_count, 3, bit_count),
    /* the type-generic classifications and comparisons of <math.h> (C17
       7.12.3, 7.12.14): the arguments as they are, unpromoted */
    ROW("__builtin_fpclassify", generic, 0, floating_classify),
    ROW("__builtin_isfinite", generic, 0, floating_classify),
    ROW("__builtin_isgreater", generic, 0, floating_classify),
    ROW("__builtin_isgreaterequal", generic, 0, floating_classify),
    ROW("__builtin_isinf", generic, 0, floating_classify),
    ROW("__builtin_isinf_sign", generic, 0, floating_classify),
    ROW("__builtin_isless", generic, 0, floating_classify),
    ROW("__builtin_islessequal", generic, 0, floating_classify),
    ROW("__builtin_islessgreater", generic, 0, floating_classify),
    ROW("__builtin_isnan", generic, 0, floating_classify),
    ROW("__builtin_isnormal", generic, 0, floating_classify),
    ROW("__builtin_issignaling", generic, 0, none),
    ROW("__builtin_isunordered", generic, 0, floating_classify),
    ROW("__builtin_signbit", generic, 0, floating_classify),
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
    case builtin_form::launder:
    case builtin_form::bswapg:
    case builtin_form::unsupported:
        return true;
    default:
        return false;
    }
}

} // namespace cplusplus
