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
    /* T __builtin_bswapg(T), T an integer type of a multiple of 8 bits */
    bswapg,
    /* a builtin of the C front end whose signature has parameters this
       front end has no types for yet: declared, a call is refused naming it */
    unsupported,
};

/* how a constant expression evaluates a call, where it may */
enum class builtin_fold : uint8_t {
    none,
    constant_p,
    lock_free,
    overflow,
    bit_count,
    floating_classify,
    floating_constant,
    /* the first argument's value */
    identity,
    bswap,
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
/* whether a call of the builtin of the row is typed by its arguments, not
   by one function type (the form is not `signature`, `generic` or
   `floating_constant`) */
bool gnu_builtin_untyped(uint16_t row);

} // namespace cplusplus

#endif
