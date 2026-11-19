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

/*
 * [expr.const]/11: "A constant expression is either a glvalue core constant
 * expression that refers to an entity that is a permitted result of a
 * constant expression (as defined below), or a prvalue core constant
 * expression whose value satisfies the following constraints".
 *
 * The one place a construct that requires a constant expression learns
 * whether it has one.  Today the answer is what the operators folded as the
 * expression was analysed (expression_meaning::evaluation); an evaluation
 * analysis could not follow (constness::unknown: a call, an object read
 * through a pointer, a class value) is unsupported here, and is where the
 * evaluator of the semantic tree is to be asked.
 */
analysis_result semantic_analysis::constant_expression(node_index at,
                                                       expression_meaning &m,
                                                       bool &constant)
{
    constant = false;
    if (m.evaluation == constness::unknown) {
        /* what the operators could not fold: the evaluator's */
        if (!m.node)
            return not_supported(at, "expr.const");
        constant_value v;
        analysis_result r = evaluate_constant(at, m.node, v, constant);
        if (r == analysis_result::well_formed && constant)
            evaluated_meaning(m, v);
        return r;
    }
    constant = m.evaluation == constness::constant;
    return analysis_result::well_formed;
}

/*
 * [expr.const]/8: "An integral constant expression is an expression of
 * integral or unscoped enumeration type, implicitly converted to a prvalue,
 * where the converted expression is a core constant expression."  The
 * caller has checked the type.
 */
analysis_result semantic_analysis::integral_constant_expression(
    node_index at, expression_meaning &m, bool &constant)
{
    analysis_result r = constant_expression(at, m, constant);
    if (r != analysis_result::well_formed || !constant)
        return r;
    /* [expr.const]/1 of C++03: "An integral constant-expression can involve
       only literals, enumerators, const variables or static data members of
       integral or enumeration types initialized with constant expressions
       ... Floating literals can appear only if they are cast to integral or
       enumeration types.": what clang folds beyond that is its extension */
    if (!tokens.files.behavior.has(language_feature::list_initialization) &&
        m.beyond_integral_constant) {
        constant = false;
        return not_supported(at, "expr.const");
    }
    return analysis_result::well_formed;
}

/*
 * [expr.const]/10: "A converted constant expression of type T is an
 * expression, implicitly converted to type T, where the converted
 * expression is a constant expression and the implicit conversion sequence
 * contains only ... integral conversions other than narrowing conversions".
 * m is an integral constant expression; a narrowing conversion is clang's
 * -Wc++11-narrowing, an error by default.
 */
analysis_result semantic_analysis::converted_constant_expression(
    node_index at, expression_meaning &m, fundamental_type to, bool &constant)
{
    constant = false;
    if (tokens.files.behavior.has(language_feature::list_initialization)) {
        bool narrows = false;
        analysis_result r = narrowing(
            at, m, qualified_type::make(type_table.fundamental(to), cv_none),
            narrows);
        if (r != analysis_result::well_formed)
            return r;
        if (narrows) {
            r = extension_used(diagnostic_id::sema_narrowing,
                               syntax(at).first_token(), nullptr);
            if (r != analysis_result::well_formed)
                return r;
        }
    }
    convert_arithmetic(m, to);
    return constant_expression(at, m, constant);
}

/*
 * [expr.const]/10: "A contextually converted constant expression of type
 * bool is an expression, contextually converted to bool, where the
 * converted expression is a constant expression and the conversion sequence
 * contains only the conversions above."
 */
analysis_result semantic_analysis::bool_constant_expression(node_index at,
                                                            syntax_child e,
                                                            bool &constant,
                                                            bool &value)
{
    constant = false;
    value = false;
    expression_meaning m;
    analysis_result r = expression(e, m);
    if (r != analysis_result::well_formed)
        return r;
    r = contextual_bool(at, m);
    if (r != analysis_result::well_formed)
        return r;
    r = constant_expression(at, m, constant);
    if (r != analysis_result::well_formed)
        return r;
    value = constant && m.value != 0;
    return analysis_result::well_formed;
}

} // namespace cplusplus
