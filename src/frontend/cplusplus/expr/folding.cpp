/*
 * folding.cpp: the operators of the arithmetic types on values, as
 * expression_meaning keeps them (an integer sign- or zero-extended to 64
 * bits, a float or double as binary64 bits).  Analysis folds with them as
 * it builds the tree (expr.compound.cpp, expr.unary.cpp), and the
 * evaluator of constant expressions runs the tree with them
 * (expr.const.evaluate.cpp): one set of rules for both.
 *
 * Each answers constant with the value in out, not_constant for what
 * [expr.pre]/4 makes undefined ("If during the evaluation of an expression,
 * the result is not mathematically defined or not in the range of
 * representable values for its type, the behavior is undefined", and
 * [expr.const]/5.7 a core constant expression may not), or unknown for what
 * is not folded here: long double, 128-bit integers, a floating-point
 * operation whose rounding a pragma may change.
 */
#include <math.h>

#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

namespace
{

/* the value of a floating-point operation in type t: computed in float for
   float, so that each operation rounds as the target's does */
double floating_operation(fundamental_type t, preprocessing_op_or_punc op,
                          double a, double b)
{
    if (t == fundamental_type::float_) {
        float x = static_cast<float>(a), y = static_cast<float>(b), r = 0;
        switch (op) {
        case preprocessing_op_or_punc::PLUS:
            r = x + y;
            break;
        case preprocessing_op_or_punc::MINUS:
            r = x - y;
            break;
        case preprocessing_op_or_punc::STAR:
            r = x * y;
            break;
        default:
            r = x / y;
            break;
        }
        return r;
    }
    switch (op) {
    case preprocessing_op_or_punc::PLUS:
        return a + b;
    case preprocessing_op_or_punc::MINUS:
        return a - b;
    case preprocessing_op_or_punc::STAR:
        return a * b;
    default:
        return a / b;
    }
}

bool folded_comparison(preprocessing_op_or_punc op)
{
    switch (op) {
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        return true;
    default:
        return false;
    }
}

/* [expr.rel]/6: "each of the operators shall yield true if the specified
   relationship is true and false if it is false"; [expr.eq] the same */
bool comparison_value(preprocessing_op_or_punc op, bool less, bool equal)
{
    switch (op) {
    case preprocessing_op_or_punc::LESS:
        return less;
    case preprocessing_op_or_punc::GREATER:
        return !less && !equal;
    case preprocessing_op_or_punc::LESS_EQUAL:
        return less || equal;
    case preprocessing_op_or_punc::GREATER_EQUAL:
        return !less;
    case preprocessing_op_or_punc::EQUAL_EQUAL:
        return equal;
    default:
        return !equal;
    }
}

} // namespace

/*
 * [expr.mul], [expr.add] (on arithmetic operands), [expr.shift],
 * [expr.bit.and], [expr.xor], [expr.or], [expr.rel] and [expr.eq] on
 * operands converted to t; a shift's right operand has its own promoted
 * type right.  A comparison's value is a bool.
 */
constness fold_binary(const implementation_defined_behavior &b,
                      bool floating_point_pragma, preprocessing_op_or_punc op,
                      fundamental_type t, fundamental_type right, uint64_t x,
                      uint64_t y, uint64_t &out)
{
    bool comparison = folded_comparison(op);
    if (t == fundamental_type::long_double)
        return constness::unknown;
    if (fundamental_type_is_floating(t)) {
        double p = floating_value(x), q = floating_value(y);
        if (comparison) {
            if (p != p || q != q)
                return constness::unknown;
            out = comparison_value(op, p < q, p == q);
            return constness::constant;
        }
        /* the rounding mode a pragma sets ([cfenv.syn], C's 7.6.1) */
        if (floating_point_pragma)
            return constness::unknown;
        double r = floating_operation(t, op, p, q);
        if ((op == preprocessing_op_or_punc::SLASH && q == 0) || isinf(r) ||
            isnan(r))
            return constness::not_constant;
        out = floating_kept(r);
        return constness::constant;
    }
    /* (a 128-bit operation is not evaluated here: integer_kept_exactly) */
    if (fundamental_type_width(b, t) >= 128)
        return constness::unknown;
    __int128 p = integer_value(b, t, x);
    if (comparison) {
        __int128 q = integer_value(b, t, y);
        out = comparison_value(op, p < q, p == q);
        return constness::constant;
    }
    __int128 q = integer_value(b, right, y);
    bool is_signed = fundamental_type_is_signed(b, t);
    unsigned width = fundamental_type_width(b, t);
    bool shift = op == preprocessing_op_or_punc::LESS_LESS ||
                 op == preprocessing_op_or_punc::GREATER_GREATER;
    __int128 r = 0;
    switch (op) {
    case preprocessing_op_or_punc::PLUS:
        r = p + q;
        break;
    case preprocessing_op_or_punc::MINUS:
        r = p - q;
        break;
    case preprocessing_op_or_punc::STAR:
        if (is_signed)
            r = p * q;
        else
            r = static_cast<__int128>(static_cast<unsigned __int128>(p) *
                                          static_cast<unsigned __int128>(q) &
                                      ~uint64_t(0));
        break;
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
        /* [expr.mul]/4: "If the second operand of / or % is zero the
           behavior is undefined. ... if the quotient a/b is representable
           in the type of the result, (a/b)*b + a%b is equal to a; otherwise,
           the behavior of both a/b and a%b is undefined." */
        if (q == 0)
            return constness::not_constant;
        if (is_signed && !integer_holds(b, t, p / q))
            return constness::not_constant;
        if (op == preprocessing_op_or_punc::SLASH)
            r = p / q;
        else
            r = p % q;
        break;
    case preprocessing_op_or_punc::AMPERSAND:
        r = p & q;
        break;
    case preprocessing_op_or_punc::CARET:
        r = p ^ q;
        break;
    case preprocessing_op_or_punc::BAR:
        r = p | q;
        break;
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
        /* [expr.shift]/1: "The behavior is undefined if the right operand
           is negative, or greater than or equal to the width of the
           promoted left operand." */
        if (q < 0 || q >= width)
            return constness::not_constant;
        if (op == preprocessing_op_or_punc::GREATER_GREATER) {
            /* [expr.shift]/3: "The value of E1 >> E2 is E1/2^E2, rounded
               down." */
            r = p >> static_cast<int>(q);
            break;
        }
        /* [expr.shift]/2: "The value of E1 << E2 is the unique value
           congruent to E1 x 2^E2 modulo 2^N" (C++20); before, for a signed
           E1, "if E1 has a signed type and non-negative value, and
           E1 x 2^E2 is representable in the corresponding unsigned type of
           the result type, then that value, converted to the result type,
           is the resulting value; otherwise, the behavior is undefined"
           (C++17 [expr.shift]/2, CWG1457) */
        r = static_cast<__int128>(static_cast<unsigned __int128>(p)
                                  << static_cast<int>(q));
        if (is_signed && !b.has(language_feature::modular_left_shift) &&
            (p < 0 || (r >> width) != 0))
            return constness::not_constant;
        break;
    default:
        return constness::unknown;
    }
    /* signed overflow: [expr.pre]/4 */
    if (is_signed && !shift && !integer_holds(b, t, r))
        return constness::not_constant;
    out = integer_wrap(b, t, static_cast<unsigned __int128>(r));
    return constness::constant;
}

/*
 * [expr.unary.op]/7, /8, /9, /10: unary +, -, ~ on an operand promoted to
 * t, ! on one converted to bool.
 */
constness fold_unary(const implementation_defined_behavior &b,
                     preprocessing_op_or_punc op, fundamental_type t,
                     uint64_t x, uint64_t &out)
{
    if (op == preprocessing_op_or_punc::PLUS) {
        out = x;
        return constness::constant;
    }
    if (op == preprocessing_op_or_punc::EXCLAMATION) {
        out = !x;
        return constness::constant;
    }
    if (t == fundamental_type::long_double)
        return constness::unknown;
    if (fundamental_type_is_floating(t)) {
        if (op != preprocessing_op_or_punc::MINUS)
            return constness::unknown;
        out = floating_kept(-floating_value(x));
        return constness::constant;
    }
    /* (a 128-bit operation is not evaluated here) */
    if (fundamental_type_width(b, t) >= 128)
        return constness::unknown;
    __int128 v = integer_value(b, t, x);
    if (op == preprocessing_op_or_punc::TILDE) {
        out = integer_wrap(b, t, ~static_cast<unsigned __int128>(v));
        return constness::constant;
    }
    if (op != preprocessing_op_or_punc::MINUS)
        return constness::unknown;
    /* [expr.unary.op]/8: "The negative of an unsigned quantity is computed
       by subtracting its value from 2^n"; a signed one that overflows is
       undefined ([expr.pre]/4) */
    if (fundamental_type_is_signed(b, t) && !integer_holds(b, t, -v))
        return constness::not_constant;
    out = integer_wrap(b, t, static_cast<unsigned __int128>(-v));
    return constness::constant;
}

} // namespace cplusplus
