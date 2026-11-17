/*
 * expr.const.cpp: [expr.const] Constant expressions: the values analysis
 * computes for them.
 *
 * [expr.const]/11: "A constant expression is either a glvalue core constant
 * expression ... or a prvalue core constant expression whose value
 * satisfies the following constraints".  Analysis evaluates the operators
 * of the fundamental types on the target's representation (aarch64 Linux:
 * implementation_defined_behavior) and gives up (constness::unknown) where
 * it does not model a value: long double, addresses, objects read through
 * pointers.  [expr.const]/5.7: "an operation that would have undefined
 * behavior" makes an expression no core constant expression: each
 * operator checks its own (signed overflow, division by zero, shifts).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

#include <string.h>

namespace cplusplus
{

bool fundamental_type_is_integral(fundamental_type t)
{
    /* [basic.fundamental]/11: "Types bool, char, wchar_t, char8_t,
       char16_t, char32_t, and the signed and unsigned integer types are
       collectively called integral types." */
    switch (t) {
    case fundamental_type::bool_:
    case fundamental_type::char_:
    case fundamental_type::signed_char:
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
    case fundamental_type::char16_t_:
    case fundamental_type::char32_t_:
    case fundamental_type::wchar_t_:
    case fundamental_type::short_int:
    case fundamental_type::unsigned_short_int:
    case fundamental_type::int_:
    case fundamental_type::unsigned_int:
    case fundamental_type::long_int:
    case fundamental_type::unsigned_long_int:
    case fundamental_type::long_long_int:
    case fundamental_type::unsigned_long_long_int:
    /* [basic.fundamental]/2: "the standard and extended signed integer
       types are collectively called signed integer types" (and /3 the
       unsigned ones) */
    case fundamental_type::int128:
    case fundamental_type::unsigned_int128:
        return true;
    default:
        return false;
    }
}

bool fundamental_type_is_floating(fundamental_type t)
{
    /* [basic.fundamental]/12: "There are three floating-point types: float,
       double, and long double." */
    return t == fundamental_type::float_ || t == fundamental_type::double_ ||
           t == fundamental_type::long_double;
}

uint64_t integer_wrap(const implementation_defined_behavior &behavior,
                      fundamental_type t, unsigned __int128 v)
{
    /* [conv.bool]/1: "A zero value ... is converted to false; any other
       value is converted to true." */
    if (t == fundamental_type::bool_) {
        if (v)
            return 1;
        return 0;
    }
    unsigned width = fundamental_type_width(behavior, t);
    uint64_t bits = static_cast<uint64_t>(v);
    if (width < 64)
        bits &= (uint64_t(1) << width) - 1;
    if (fundamental_type_is_signed(behavior, t) && width < 64 &&
        (bits >> (width - 1)) & 1)
        bits |= ~((uint64_t(1) << width) - 1);
    return bits;
}

bool integer_holds(const implementation_defined_behavior &behavior,
                   fundamental_type t, __int128 v)
{
    if (t == fundamental_type::bool_)
        return v == 0 || v == 1;
    unsigned width = fundamental_type_width(behavior, t);
    __int128 one = 1;
    if (width >= 128)
        return fundamental_type_is_signed(behavior, t) || v >= 0;
    if (fundamental_type_is_signed(behavior, t))
        return v >= -(one << (width - 1)) && v < (one << (width - 1));
    return v >= 0 && v < (one << width);
}

__int128 integer_value(const implementation_defined_behavior &behavior,
                       fundamental_type t, uint64_t kept)
{
    if (fundamental_type_is_signed(behavior, t))
        return static_cast<int64_t>(kept);
    return kept;
}

bool integer_kept_exactly(const implementation_defined_behavior &behavior,
                          fundamental_type t, unsigned __int128 v)
{
    if (fundamental_type_width(behavior, t) < 128)
        return true;
    if (fundamental_type_is_signed(behavior, t))
        return static_cast<__int128>(v) ==
               static_cast<int64_t>(static_cast<uint64_t>(v));
    return v == static_cast<uint64_t>(v);
}

constness combined_constness(constness a, constness b)
{
    if (a == constness::not_constant || b == constness::not_constant)
        return constness::not_constant;
    if (a == constness::unknown || b == constness::unknown)
        return constness::unknown;
    return constness::constant;
}

double floating_value(uint64_t kept)
{
    double v;
    memcpy(&v, &kept, sizeof v);
    return v;
}

uint64_t floating_kept(double v)
{
    uint64_t kept;
    memcpy(&kept, &v, sizeof kept);
    return kept;
}

} // namespace cplusplus
