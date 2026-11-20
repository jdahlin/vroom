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
};

struct gnu_builtin {
    const char *name;
    builtin_form form;
    /* the form's own number (the query, the va_list builtin, the bit count) */
    uint8_t which;
    builtin_fold fold;
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
