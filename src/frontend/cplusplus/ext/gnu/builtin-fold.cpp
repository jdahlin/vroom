/*
 * builtin-fold.cpp: [ext:gnu_library_builtins] the builtins a constant
 * expression evaluates, as clang's ExprConstant.cpp does: the bit
 * operations, the overflow builtins, the classifications of floating
 * values and the string and memory functions on arrays of constant
 * objects.  evaluate_call looks the builtin up in the registry
 * (builtin-registry.cpp) and calls evaluate_builtin_fold; a call whose
 * arguments are not constants, or that clang does not fold, is not a
 * constant expression.
 */
#include "../../cplusplus-internal.h"
#include "frontend/builtin-fold.h"
#include "frontend/cplusplus/builtin-registry.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* the width in bits of an integer type, a bool's being its byte */
static unsigned integer_bits(const implementation_defined_behavior &b,
                             fundamental_type f)
{
    if (f == fundamental_type::bool_)
        return 8;
    return fundamental_type_width(b, f);
}

/* the operation of frontend/builtin-fold.h that a registry row's number
   names */
static int shared_integer_operation(builtin_integer_op op)
{
    switch (op) {
    case builtin_integer_op::clz:
        return vfold_clz;
    case builtin_integer_op::ctz:
        return vfold_ctz;
    case builtin_integer_op::popcount:
        return vfold_popcount;
    case builtin_integer_op::ffs:
        return vfold_ffs;
    case builtin_integer_op::parity:
        return vfold_parity;
    case builtin_integer_op::clrsb:
        return vfold_clrsb;
    case builtin_integer_op::bswap:
        return vfold_bswap;
    case builtin_integer_op::bitreverse:
        return vfold_bitreverse;
    case builtin_integer_op::rotate_left:
        return vfold_rotate_left;
    case builtin_integer_op::rotate_right:
        return vfold_rotate_right;
    case builtin_integer_op::absolute:
        return vfold_absolute;
    case builtin_integer_op::expect:
        return vfold_expect;
    }
    return vfold_none;
}

/* [expr.const]: the integer builtins, on the first argument's width */
constness semantic_analysis::evaluate_integer_builtin(
    constant_evaluation &state, const gnu_builtin &b, const semantic_node &call,
    constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    constant_value x[2];
    for (uint32_t i = 0; i < call.count && i < 2; i++) {
        constness c = evaluate_node(state, items[i], x[i]);
        if (c != constness::constant)
            return c;
        if (x[i].kind != constant_value_kind::integer)
            return constness::unknown;
    }
    fundamental_type f = fundamental_of(semantics[items[0]].type);
    if (f == fundamental_type::none)
        return constness::unknown;
    unsigned width = integer_bits(tokens.files.behavior, f);
    unsigned __int128 result = 0;
    int operation =
        shared_integer_operation(static_cast<builtin_integer_op>(b.which));
    switch (operation) {
    case vfold_rotate_left:
    case vfold_rotate_right:
        if (call.count != 2)
            return constness::unknown;
        result = vfold_rotate(x[0].integer, width, x[1].integer,
                              operation == vfold_rotate_right);
        break;
    case vfold_expect:
        result = x[0].integer;
        break;
    default:
        /* clang: "__builtin_clz(0)" is not a constant (undefined) */
        if (!vfold_integer(operation, x[0].integer, width, &result))
            return constness::not_constant;
        break;
    }
    fundamental_type r = fundamental_of(call.type);
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = integer_wrap(tokens.files.behavior, r, result);
    return constness::constant;
}

/* a floating value of a float argument kept as the double it converts to */
static bool is_single(fundamental_type f)
{
    return f == fundamental_type::float_;
}

/* the classes of fpclassify, in clang's order of its five arguments */
enum fp_class { fp_nan, fp_infinite, fp_normal, fp_subnormal, fp_zero };

static fp_class classify_floating_value(double d, bool single)
{
    if (__builtin_isnan(d))
        return fp_nan;
    if (__builtin_isinf(d))
        return fp_infinite;
    if (d == 0)
        return fp_zero;
    double least = 2.2250738585072014e-308;
    if (single)
        least = 1.17549435082228750797e-38;
    if (__builtin_fabs(d) < least)
        return fp_subnormal;
    return fp_normal;
}

constness semantic_analysis::evaluate_floating_classification(
    constant_evaluation &state, const gnu_builtin &b, const semantic_node &call,
    constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    uint32_t last = call.count - 1;
    constant_value x[2];
    /* fpclassify's five leading arguments are the classes' values */
    uint32_t first = 0;
    if (static_cast<builtin_float_op>(b.which) == builtin_float_op::fpclassify)
        first = 5;
    uint32_t operands = call.count - first;
    if (operands < 1 || operands > 2)
        return constness::unknown;
    (void)last;
    bool single = true;
    for (uint32_t i = 0; i < operands; i++) {
        constness c = evaluate_node(state, items[first + i], x[i]);
        if (c != constness::constant)
            return c;
        fundamental_type f = fundamental_of(semantics[items[first + i]].type);
        if (!fundamental_type_is_floating(f) &&
            !fundamental_type_is_integral(f))
            return constness::unknown;
        if (!is_single(f))
            single = false;
        if (x[i].kind == constant_value_kind::integer)
            x[i].floating =
                static_cast<double>(static_cast<__int128>(x[i].integer));
        else if (x[i].kind != constant_value_kind::floating)
            return constness::unknown;
    }
    double a = x[0].floating, c2 = x[1].floating;
    fp_class k = classify_floating_value(a, single);
    bool unordered = __builtin_isnan(a) || __builtin_isnan(c2);
    long result = 0;
    switch (static_cast<builtin_float_op>(b.which)) {
    case builtin_float_op::isnan:
        result = k == fp_nan;
        break;
    case builtin_float_op::isinf:
        result = k == fp_infinite;
        break;
    case builtin_float_op::isfinite:
        result = k != fp_nan && k != fp_infinite;
        break;
    case builtin_float_op::isnormal:
        result = k == fp_normal;
        break;
    case builtin_float_op::signbit:
        result = __builtin_signbit(a) != 0;
        break;
    case builtin_float_op::isinf_sign:
        if (k == fp_infinite)
            result = 1;
        if (k == fp_infinite && __builtin_signbit(a))
            result = -1;
        break;
    case builtin_float_op::fpclassify: {
        /* the argument of the class: nan, infinite, normal, subnormal,
           zero */
        constant_value chosen;
        constness c = evaluate_node(state, items[k], chosen);
        if (c != constness::constant)
            return c;
        v = chosen;
        return constness::constant;
    }
    case builtin_float_op::isgreater:
        result = !unordered && a > c2;
        break;
    case builtin_float_op::isgreaterequal:
        result = !unordered && a >= c2;
        break;
    case builtin_float_op::isless:
        result = !unordered && a < c2;
        break;
    case builtin_float_op::islessequal:
        result = !unordered && a <= c2;
        break;
    case builtin_float_op::islessgreater:
        result = !unordered && a != c2;
        break;
    case builtin_float_op::isunordered:
        result = unordered;
        break;
    case builtin_float_op::fabs:
        v = constant_value();
        v.kind = constant_value_kind::floating;
        v.floating = __builtin_fabs(a);
        return constness::constant;
    case builtin_float_op::copysign:
        v = constant_value();
        v.kind = constant_value_kind::floating;
        v.floating = __builtin_copysign(a, c2);
        return constness::constant;
    }
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = static_cast<unsigned __int128>(static_cast<__int128>(result));
    return constness::constant;
}

/*
 * The string and memory builtins on a pointer into an array of constant
 * objects ([expr.const]; clang's ExprConstant.cpp, evaluating strlen,
 * strcmp, strncmp, memcmp, memchr, __builtin_char_memchr and the wide
 * forms): the element values are the array's, an access past the end is not
 * a constant expression.
 */
constness semantic_analysis::evaluate_string_builtin(constant_evaluation &state,
                                                     const gnu_builtin &b,
                                                     const semantic_node &call,
                                                     constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    /* (memcmp and memchr take void pointers: the pointer is the argument
       before the conversions to void and from a more cv-qualified type,
       which is the same address, and its type tells the elements') */
    uint32_t nodes[3] = {0, 0, 0};
    constant_value x[3];
    for (uint32_t i = 0; i < call.count && i < 3; i++) {
        nodes[i] = items[i];
        while (semantics[nodes[i]].kind == semantic_kind::conversion &&
               (static_cast<conversion_kind>(semantics[nodes[i]].op) ==
                    conversion_kind::pointer ||
                static_cast<conversion_kind>(semantics[nodes[i]].op) ==
                    conversion_kind::qualification_cast))
            nodes[i] = semantics[nodes[i]].a;
        constness c = evaluate_node(state, nodes[i], x[i]);
        if (c != constness::constant)
            return c;
    }
    auto op = static_cast<builtin_string_op>(b.which);
    auto element_at = [&state](const constant_value &p, uint64_t i,
                               uint64_t &value) {
        if (!p.count || !p.extent || p.offset + i >= p.extent)
            return false;
        value = static_cast<uint64_t>(
            state.elements[p.base + p.offset + i].integer);
        return true;
    };
    qualified_type from = semantics[nodes[0]].type;
    qualified_type element;
    if (is_pointer(from))
        element = type_table[from.unqualified()].operand;
    else
        return constness::unknown;
    fundamental_type f = fundamental_of(element);
    if (f == fundamental_type::none)
        return constness::unknown;
    unsigned width = integer_bits(tokens.files.behavior, f);
    bool narrow = op != builtin_string_op::wcslen &&
                  op != builtin_string_op::wmemcmp &&
                  op != builtin_string_op::wmemchr;
    if (narrow && width != 8)
        return constness::unknown;
    uint64_t mask = ~uint64_t(0);
    if (width < 64)
        mask = (uint64_t(1) << width) - 1;
    bool wide_signed = fundamental_type_is_signed(tokens.files.behavior, f);
    auto signed_value = [&](uint64_t u) -> int64_t {
        u &= mask;
        if (!narrow && wide_signed && width < 64 && (u >> (width - 1)))
            return static_cast<int64_t>(u | ~mask);
        return static_cast<int64_t>(u);
    };
    v = constant_value();
    v.kind = constant_value_kind::integer;
    if (op == builtin_string_op::strlen || op == builtin_string_op::wcslen) {
        uint64_t n = 0, e = 0;
        for (;; n++) {
            if (!element_at(x[0], n, e))
                return constness::not_constant;
            if ((e & mask) == 0)
                break;
        }
        v.integer = n;
        return constness::constant;
    }
    if (op == builtin_string_op::strcmp || op == builtin_string_op::strncmp) {
        uint64_t limit = ~uint64_t(0);
        if (op == builtin_string_op::strncmp)
            limit = static_cast<uint64_t>(x[2].integer);
        for (uint64_t n = 0; n < limit; n++) {
            uint64_t a = 0, c = 0;
            if (!element_at(x[0], n, a) || !element_at(x[1], n, c))
                return constness::not_constant;
            if ((a & mask) != (c & mask)) {
                int sign = 1;
                if ((a & mask) < (c & mask))
                    sign = -1;
                v.integer =
                    static_cast<unsigned __int128>(static_cast<__int128>(sign));
                return constness::constant;
            }
            if ((a & mask) == 0)
                break;
        }
        v.integer = 0;
        return constness::constant;
    }
    if (op == builtin_string_op::memcmp || op == builtin_string_op::wmemcmp) {
        uint64_t limit = static_cast<uint64_t>(x[2].integer);
        for (uint64_t n = 0; n < limit; n++) {
            uint64_t a = 0, c = 0;
            if (!element_at(x[0], n, a) || !element_at(x[1], n, c))
                return constness::not_constant;
            int64_t l = signed_value(a), r = signed_value(c);
            if (l != r) {
                int sign = 1;
                if (l < r)
                    sign = -1;
                v.integer =
                    static_cast<unsigned __int128>(static_cast<__int128>(sign));
                return constness::constant;
            }
        }
        v.integer = 0;
        return constness::constant;
    }
    /* memchr, __builtin_char_memchr, wmemchr: the pointer to the first
       element equal to the value, null if none in the first n */
    uint64_t wanted = static_cast<uint64_t>(x[1].integer) & mask;
    uint64_t limit = static_cast<uint64_t>(x[2].integer);
    for (uint64_t n = 0; n < limit; n++) {
        uint64_t e = 0;
        if (!element_at(x[0], n, e))
            return constness::not_constant;
        if ((e & mask) == wanted)
            return offset_pointer(x[0], static_cast<__int128>(n), v);
    }
    v = constant_value();
    v.kind = constant_value_kind::pointer;
    return constness::constant;
}

/*
 * __builtin_add_overflow (a, b, &r) and the subtraction and multiplication
 * ones, typed forms included: r gets the exact result converted to its
 * type; the value is whether that changed it.  A constant expression
 * writes the object the pointer designates.
 */
constness semantic_analysis::evaluate_overflow_builtin(
    constant_evaluation &state, const gnu_builtin &b, const semantic_node &call,
    constant_value &v)
{
    const uint32_t *items = semantics.items(call.first);
    if (call.count != 3)
        return constness::unknown;
    constant_value x[3];
    for (uint32_t i = 0; i < 3; i++) {
        constness c = evaluate_node(state, items[i], x[i]);
        if (c != constness::constant)
            return c;
    }
    if (x[0].kind != constant_value_kind::integer ||
        x[1].kind != constant_value_kind::integer ||
        x[2].kind != constant_value_kind::pointer || !x[2].count ||
        x[2].extent > 1)
        return constness::unknown;
    fundamental_type fa = fundamental_of(semantics[items[0]].type);
    fundamental_type fb = fundamental_of(semantics[items[1]].type);
    fundamental_type fr = fundamental_of(
        type_table[semantics[items[2]].type.unqualified()].operand);
    if (fa == fundamental_type::none || fb == fundamental_type::none ||
        fr == fundamental_type::none ||
        fundamental_type_width(tokens.files.behavior, fa) > 64 ||
        fundamental_type_width(tokens.files.behavior, fb) > 64 ||
        fundamental_type_width(tokens.files.behavior, fr) > 64)
        return constness::unknown;
    __int128 l =
        integer_value(tokens.files.behavior, fa,
                      integer_wrap(tokens.files.behavior, fa, x[0].integer));
    __int128 r =
        integer_value(tokens.files.behavior, fb,
                      integer_wrap(tokens.files.behavior, fb, x[1].integer));
    __int128 exact = 0;
    switch (b.which) {
    case 1:
        exact = l + r;
        break;
    case 2:
        exact = l - r;
        break;
    default:
        exact = l * r;
        break;
    }
    /* the products of two 64-bit values may not fit 128 bits as the
       unsigned ones; the operands here are at most 64 bits wide, and
       unsigned 64-bit ones the __int128 holds */
    uint64_t kept = integer_wrap(tokens.files.behavior, fr,
                                 static_cast<unsigned __int128>(exact));
    __int128 stored = integer_value(tokens.files.behavior, fr, kept);
    constant_value result = value_of(fr, kept);
    uint32_t place = x[2].first;
    if (place_read_only(state, place))
        return constness::not_constant;
    place_value(state, place) = result;
    v = constant_value();
    v.kind = constant_value_kind::integer;
    v.integer = stored != exact;
    return constness::constant;
}

constness semantic_analysis::evaluate_builtin_fold(constant_evaluation &state,
                                                   const gnu_builtin &b,
                                                   const semantic_node &call,
                                                   constant_value &v)
{
    switch (b.fold) {
    case builtin_fold::integer:
        return evaluate_integer_builtin(state, b, call, v);
    case builtin_fold::floating:
        return evaluate_floating_classification(state, b, call, v);
    case builtin_fold::string:
        return evaluate_string_builtin(state, b, call, v);
    case builtin_fold::overflow:
        return evaluate_overflow_builtin(state, b, call, v);
    default:
        break;
    }
    return constness::unknown;
}

} // namespace cplusplus
