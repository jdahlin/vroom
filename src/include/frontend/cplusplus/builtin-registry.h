#ifndef VCC_FRONTEND_CPLUSPLUS_BUILTIN_REGISTRY_H
#define VCC_FRONTEND_CPLUSPLUS_BUILTIN_REGISTRY_H
/*
 * builtin-registry.h: [ext:gnu_library_builtins] the registry of the
 * compiler builtins (frontend/cplusplus/ext/gnu/builtin-registry.cpp).
 */
#include <stddef.h>
#include <stdint.h>

namespace cplusplus
{

/* how a call of a builtin is checked and typed */
enum class builtin_form : uint8_t {
    /* the prototype the C front end's builtin table gives: one function type,
       the standard conversions ([expr.call]) */
    signature,
    /* type-generic (<math.h>'s classifications): the arguments as they
       are, unpromoted; the result by the table's signature */
    generic,
    floating_constant,
    /* __builtin_constant_p and the lock-free queries */
    query,
    va_list,
    overflow,
    atomic,
    bit_count,
    /* a builtin the C front end has its own node for that is a call of the
       C library function here, as clang's: __builtin_strlen */
    library,
    /* a function of the prototype in `proto` that is a call of the C
       library function `symbol`: __builtin_char_memchr */
    library_alias,
    /* T *__builtin_launder(T *) */
    launder,
    /* void __builtin_clear_padding(T *) */
    clear_padding,
    /* T __builtin_bswapg(T), T an integer type of a multiple of 8 bits */
    bswapg,
    /* C23's <stdbit.h> functions and __builtin_bitreverseg, type-generic over
       the integer types (builtin_stdc_op): the result by the operation */
    stdc_bits,
    /* __builtin_addc (x, y, carry_in, &carry_out) and the subtraction and the
       typed forms: the C table's signature with the carry out a pointer to
       the type */
    carry,
    /* _Complex T __builtin_complex (T, T) */
    complex,
    /* __builtin_creal, __builtin_cimag: the real or imaginary part of a
       complex number; __builtin_conj: its conjugate */
    complex_part,
    complex_conjugate,
    /* __builtin_invoke (f, args...): analysed as the call it stands for */
    invoke,
    /* a builtin of the C front end whose signature has parameters this
       front end has no types for yet: declared, a call is refused naming it */
    unsupported,
};

/* how a constant expression evaluates a call, where it may (the row's
   `which` says which operation of the group) */
enum class builtin_fold : uint8_t {
    none,
    constant_p,
    lock_free,
    overflow,
    bit_count,
    /* the integer builtins (builtin_integer_op) */
    integer,
    /* the classifications, comparisons and sign operations of floating
       values (builtin_float_op) */
    floating,
    /* the string and memory functions on arrays of constants
       (builtin_string_op) */
    string,
    /* __builtin_object_size and __builtin_dynamic_object_size of a pointer
       into an array of constants */
    object_size,
    /* __builtin_classify_type: the class number of the argument's type,
       which is not evaluated */
    classify_type,
    floating_constant,
    /* the first argument's value */
    identity,
    bswap,
    /* the stdc_bits family (builtin_stdc_op) */
    stdc_bits,
};

/* the operations of the stdc_bits form, [stdbit.h.syn] (C23 7.18) */
enum class builtin_stdc_op : uint8_t {
    leading_zeros,
    leading_ones,
    trailing_ones,
    first_leading_zero,
    first_leading_one,
    first_trailing_zero,
    first_trailing_one,
    count_zeros,
    bit_width,
    has_single_bit,
    bit_ceil,
    bit_floor,
    rotate_left,
    rotate_right,
    bitreverse,
};

enum class builtin_integer_op : uint8_t {
    clz,
    ctz,
    popcount,
    ffs,
    parity,
    clrsb,
    bswap,
    bitreverse,
    rotate_left,
    rotate_right,
    absolute,
    expect,
};

enum class builtin_float_op : uint8_t {
    isnan,
    isinf,
    isfinite,
    isnormal,
    signbit,
    isinf_sign,
    fpclassify,
    isgreater,
    isgreaterequal,
    isless,
    islessequal,
    islessgreater,
    isunordered,
    fabs,
    copysign,
    iszero,
    issubnormal,
    isfpclass,
};

enum class builtin_string_op : uint8_t {
    strlen,
    strcmp,
    strncmp,
    memcmp,
    memchr,
    wcslen,
    wmemcmp,
    wmemchr,
};

/* what lowering makes of a call, where it is not the C front end's builtin
   node of the same name */
enum class builtin_lowering : uint8_t {
    node,
    /* the call is its argument */
    identity,
    /* the arguments evaluated for their effects, no value */
    discard,
    /* __builtin_trap's node */
    trap,
    /* the C front end's byte swap of the argument's width */
    bswap,
    /* zero the padding bytes of the pointee object */
    clear_padding,
    /* a call that analysis and constant evaluation know and lowering does
       not: refused with the reason */
    refuse,
};

struct gnu_builtin {
    const char *name;
    builtin_form form;
    /* the form's own number (the query, the va_list builtin, the bit count) */
    uint8_t which;
    builtin_fold fold;
    builtin_lowering lower;
    /* the prototype in the letters of the C front end's builtin table where
       it is not the table's, and the library function a library_alias calls */
    const char *proto;
    const char *symbol;
};

/* the row of the builtins of form `unsupported` (they share it) */
constexpr uint16_t gnu_builtin_unsupported_row = 2;

/* the row of the builtin spelled so (rows count from 1, in row), or null */
const gnu_builtin *gnu_builtin_find(const char *spelling, size_t length,
                                    uint16_t *row);
const gnu_builtin &gnu_builtin_at(uint16_t row);
/* whether a constant expression evaluates the builtin: the fold table's
   row (frontend/builtin-fold.h) for the C++ language says so; a registry row
   names how, the table says whether, and the same table answers
   __has_constexpr_builtin */
bool gnu_builtin_folds(const gnu_builtin &b);
/* whether a call of the builtin of the row is typed by its arguments, not
   by one function type (the form is not `signature`, `generic` or
   `floating_constant`) */
bool gnu_builtin_untyped(uint16_t row);

} // namespace cplusplus

#endif
