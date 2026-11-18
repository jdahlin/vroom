/*
 * conv.cpp: [conv] Standard conversions, and [expr.arith.conv] the usual
 * arithmetic conversions, for the types analysis has rules for: the
 * fundamental types, pointers to them and to functions.
 *
 * [conv]/1: "Standard conversions are implicit conversions with built-in
 * meaning."  A conversion here changes an expression_meaning: its type,
 * its value category and, when it is a constant, its value.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

#include <math.h>

namespace cplusplus
{

fundamental_type semantic_analysis::fundamental_of(qualified_type t) const
{
    const struct type record = type_table[t.unqualified()];
    if (record.kind != type_kind::fundamental)
        return fundamental_type::none;
    return record.fundamental;
}

bool semantic_analysis::is_integral(qualified_type t) const
{
    return fundamental_type_is_integral(fundamental_of(t));
}

bool semantic_analysis::is_floating(qualified_type t) const
{
    return fundamental_type_is_floating(fundamental_of(t));
}

bool semantic_analysis::is_arithmetic(qualified_type t) const
{
    /* [basic.fundamental]/12: "Integral and floating-point types are
       collectively called arithmetic types." */
    return is_integral(t) || is_floating(t);
}

bool semantic_analysis::is_pointer(qualified_type t) const
{
    return type_table[t.unqualified()].kind == type_kind::pointer;
}

bool semantic_analysis::is_object_pointer(qualified_type t) const
{
    if (!is_pointer(t))
        return false;
    qualified_type pointee = type_table[t.unqualified()].operand;
    const struct type record = type_table[pointee.unqualified()];
    return record.kind != type_kind::function &&
           !(record.kind == type_kind::fundamental &&
             record.fundamental == fundamental_type::void_);
}

/*
 * [basic.lval]/6: "Whenever a glvalue appears as an operand of an operator
 * that expects a prvalue for that operand, the lvalue-to-rvalue,
 * array-to-pointer, or function-to-pointer standard conversions are
 * applied to convert the expression to a prvalue."
 */
analysis_result semantic_analysis::operand_value(node_index at,
                                                 expression_meaning &m)
{
    if (m.node &&
        semantics[m.node].kind == semantic_kind::bound_member_function)
        /* [expr.ref]/6.3.2: "The expression can be used only as the
           left-hand operand of a member function call." */
        return ill_formed(at,
                          "a non-static member function named by a class "
                          "member access can be used only as the left-hand "
                          "operand of a member function call",
                          "expr.ref");
    if (m.category == value_category::prvalue)
        return analysis_result::well_formed;
    const struct type record = type_table[m.type.unqualified()];
    constness address = constness::unknown;
    if (m.evaluation == constness::not_constant)
        address = constness::not_constant;
    conversion_kind kind = conversion_kind::lvalue_to_rvalue;
    if (record.kind == type_kind::array) {
        kind = conversion_kind::array_to_pointer;
        /* [conv.array]/1: "An lvalue or rvalue of type "array of N T" or
           "array of unknown bound of T" can be converted to a prvalue of
           type "pointer to T"." */
        m.type = qualified_type::make(type_table.pointer_to(record.operand),
                                      cv_none);
        m.evaluation = address;
    } else if (record.kind == type_kind::function) {
        kind = conversion_kind::function_to_pointer;
        /* [conv.func]/1: "An lvalue of function type T can be converted to
           a prvalue of type "pointer to T"." */
        m.type = qualified_type::make(type_table.pointer_to(m.type), cv_none);
        m.evaluation = address;
    } else {
        /* [conv.lval]/1: "A glvalue of a non-function, non-array type T can
           be converted to a prvalue. ... If T is a non-class type, the type
           of the prvalue is the cv-unqualified version of T." */
        constness read = constness::unknown;
        if (m.evaluation == constness::not_constant)
            read = constness::not_constant;
        else if (m.entity &&
                 entity_table[m.entity].kind == entity_kind::variable) {
            /* [expr.const]/5.8: "an lvalue-to-rvalue conversion unless it
               is applied to a non-volatile glvalue that refers to an object
               that is usable in constant expressions" */
            const entity &e = entity_table[m.entity];
            read = e.usable;
            if (e.usable == constness::constant)
                m.value = e.value;
            if (m.type.cv() & cv_volatile)
                read = constness::not_constant;
        }
        m.type = qualified_type::make(m.type.unqualified(), cv_none);
        m.evaluation = read;
    }
    m.category = value_category::prvalue;
    converted(m, kind);
    m.entity = 0;
    /* the pointer a string literal converts to keeps the literal: C++03's
       conversion to a pointer to non-const characters needs it */
    if (record.kind != type_kind::array)
        m.string_literal = 0;
    m.zero_literal = false;
    return analysis_result::well_formed;
}

namespace
{

/* [conv.rank]/1: the ranks of the integer types after promotion, and of
   those below int */
unsigned integer_rank(fundamental_type t)
{
    switch (t) {
    case fundamental_type::bool_:
        return 0;
    case fundamental_type::char_:
    case fundamental_type::signed_char:
    case fundamental_type::unsigned_char:
    case fundamental_type::char8_t_:
        return 1;
    case fundamental_type::short_int:
    case fundamental_type::unsigned_short_int:
        return 2;
    case fundamental_type::int_:
    case fundamental_type::unsigned_int:
        return 3;
    case fundamental_type::long_int:
    case fundamental_type::unsigned_long_int:
        return 4;
    case fundamental_type::long_long_int:
    case fundamental_type::unsigned_long_long_int:
        return 5;
    /* [conv.rank]/1.3: "The rank of long long int shall be greater than
       the rank of long int"; /1.7 an extended type's rank relative to
       another of the same width is implementation-defined, and wider is
       greater (/1.1 [impl.clang]) */
    case fundamental_type::int128:
    case fundamental_type::unsigned_int128:
        return 6;
    default:
        return 3;
    }
}

fundamental_type unsigned_of(fundamental_type t)
{
    switch (t) {
    case fundamental_type::int_:
        return fundamental_type::unsigned_int;
    case fundamental_type::long_int:
        return fundamental_type::unsigned_long_int;
    case fundamental_type::long_long_int:
        return fundamental_type::unsigned_long_long_int;
    case fundamental_type::int128:
        return fundamental_type::unsigned_int128;
    default:
        return t;
    }
}

/* [conv.prom]: the type an integral type promotes to */
fundamental_type promoted_type(const implementation_defined_behavior &b,
                               fundamental_type t)
{
    switch (t) {
    case fundamental_type::bool_:
        /* [conv.prom]/6: "A prvalue of type bool can be converted to a
           prvalue of type int" */
        return fundamental_type::int_;
    case fundamental_type::char16_t_:
    case fundamental_type::char32_t_:
    case fundamental_type::wchar_t_: {
        /* [conv.prom]/2: "the first of the following types that can
           represent all the values of its underlying type: int, unsigned
           int, long, unsigned long, long long, or unsigned long long" */
        static const fundamental_type order[] = {
            fundamental_type::int_,
            fundamental_type::unsigned_int,
            fundamental_type::long_int,
            fundamental_type::unsigned_long_int,
            fundamental_type::long_long_int,
            fundamental_type::unsigned_long_long_int,
        };
        unsigned width = fundamental_type_width(b, t);
        bool is_signed = fundamental_type_is_signed(b, t);
        for (fundamental_type candidate : order) {
            unsigned w = fundamental_type_width(b, candidate);
            bool s = fundamental_type_is_signed(b, candidate);
            if ((s && is_signed && w >= width) ||
                (s && !is_signed && w > width) ||
                (!s && !is_signed && w >= width))
                return candidate;
        }
        return t;
    }
    default:
        /* [conv.prom]/1: "A prvalue of an integer type other than bool,
           char16_t, char32_t, or wchar_t whose integer conversion rank is
           less than the rank of int can be converted to a prvalue of type
           int if int can represent all the values of the source type" */
        if (integer_rank(t) < integer_rank(fundamental_type::int_))
            return fundamental_type::int_;
        return t;
    }
}

} // namespace

fundamental_type semantic_analysis::promotion_of(fundamental_type t) const
{
    return promoted_type(tokens.files.behavior, t);
}

void semantic_analysis::promote(expression_meaning &m)
{
    fundamental_type t = fundamental_of(m.type);
    if (!fundamental_type_is_integral(t))
        return;
    fundamental_type to = integral_promotion(m);
    if (to != t)
        convert_arithmetic(m, to);
}

fundamental_type
semantic_analysis::integral_promotion(const expression_meaning &m) const
{
    fundamental_type t = fundamental_of(m.type);
    if (!fundamental_type_is_integral(t))
        return t;
    fundamental_type to = promoted_type(tokens.files.behavior, t);
    if (m.bit_field_width && t != fundamental_type::bool_) {
        /* [conv.prom]/5: "A prvalue for an integral bit-field can be
           converted to a prvalue of type int if int can represent all the
           values of the bit-field; otherwise, it can be converted to
           unsigned int if unsigned int can represent all the values of the
           bit-field.  If the bit-field is larger yet, no integral promotion
           applies to it." */
        const implementation_defined_behavior &b = tokens.files.behavior;
        unsigned width = m.bit_field_width;
        unsigned int_width = fundamental_type_width(b, fundamental_type::int_);
        bool is_signed = fundamental_type_is_signed(b, t);
        if (width < int_width || (is_signed && width == int_width))
            to = fundamental_type::int_;
        else if (!is_signed && width <= int_width)
            to = fundamental_type::unsigned_int;
        else
            to = t;
    }
    return to;
}

entity_index semantic_analysis::enumeration_of(qualified_type t) const
{
    const struct type record = type_table[t.unqualified()];
    if (record.kind != type_kind::enumeration ||
        entity_table[record.entity].underlying == fundamental_type::none)
        return 0;
    return record.entity;
}

/* the value is unchanged: an enumeration's value is kept as its
   underlying type's is */
bool semantic_analysis::enumeration_as_underlying(expression_meaning &m)
{
    entity_index e = enumeration_of(m.type);
    if (!e)
        return false;
    m.type = qualified_type::make(
        type_table.fundamental(entity_table[e].underlying), cv_none);
    m.zero_literal = false;
    converted(m, conversion_kind::integral);
    return true;
}

/*
 * [conv.prom]/4: "A prvalue of an unscoped enumeration type whose
 * underlying type is not fixed can be converted to a prvalue of the first
 * of the following types that can represent all the values of the
 * enumeration: int, unsigned int, long int, unsigned long int, long long
 * int, or unsigned long long int."  The underlying type chosen for such an
 * enumeration is that type (enum_specifier).  /5: "A prvalue of an
 * unscoped enumeration type whose underlying type is fixed can be
 * converted to a prvalue of its underlying type.  Moreover, if integral
 * promotion can be applied to its underlying type, a prvalue of an
 * unscoped enumeration type whose underlying type is fixed can also be
 * converted to a prvalue of the promoted underlying type."
 */
bool semantic_analysis::promote_enumeration(expression_meaning &m)
{
    entity_index e = enumeration_of(m.type);
    if (!e || entity_table[e].scoped)
        return false;
    enumeration_as_underlying(m);
    if (entity_table[e].fixed_underlying)
        promote(m);
    else
        convert_arithmetic(m, entity_table[e].promoted);
    return true;
}

void semantic_analysis::enumeration_operands(expression_meaning &a,
                                             expression_meaning &b,
                                             bool comparison)
{
    entity_index x = enumeration_of(a.type), y = enumeration_of(b.type);
    if (comparison && x && x == y && entity_table[x].scoped) {
        enumeration_as_underlying(a);
        enumeration_as_underlying(b);
        return;
    }
    promote_enumeration(a);
    promote_enumeration(b);
}

void semantic_analysis::convert_arithmetic(expression_meaning &m,
                                           fundamental_type to)
{
    fundamental_type from = fundamental_of(m.type);
    arithmetic_value(m, to);
    /* a converted value is no bit-field's, of the same type too (a cast
       to the bit-field's type) */
    m.bit_field_width = 0;
    if (from == to)
        return;
    conversion_kind kind = conversion_kind::integral;
    if (to == fundamental_type::bool_)
        kind = conversion_kind::boolean;
    else if (fundamental_type_is_floating(from) &&
             fundamental_type_is_floating(to))
        kind = conversion_kind::floating;
    else if (fundamental_type_is_floating(from) ||
             fundamental_type_is_floating(to))
        kind = conversion_kind::floating_integral;
    converted(m, kind);
}

void semantic_analysis::arithmetic_value(expression_meaning &m,
                                         fundamental_type to)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    fundamental_type from = fundamental_of(m.type);
    m.type = qualified_type::make(type_table.fundamental(to), cv_none);
    m.zero_literal = false;
    if (fundamental_type_is_floating(to))
        m.beyond_integral_constant = true;
    if (m.evaluation != constness::constant || from == to)
        return;
    if (from == fundamental_type::long_double ||
        to == fundamental_type::long_double ||
        ((fundamental_type_is_floating(from) ||
          fundamental_type_is_floating(to)) &&
         tokens.files.floating_point_pragma)) {
        /* binary128: not evaluated here; nor a conversion whose rounding a
           pragma may change */
        m.evaluation = constness::unknown;
        return;
    }
    if (fundamental_type_is_integral(from)) {
        __int128 v = integer_value(b, from, m.value);
        if (fundamental_type_is_integral(to)) {
            /* [conv.integral]/3 */
            if (!integer_kept_exactly(b, to,
                                      static_cast<unsigned __int128>(v))) {
                m.evaluation = constness::unknown;
                return;
            }
            m.value = integer_wrap(b, to, static_cast<unsigned __int128>(v));
            return;
        }
        /* [conv.fpint]/2: "If the value being converted is in the range of
           values that can be represented but the value cannot be
           represented exactly, it is an implementation-defined choice of
           either the next lower or higher representable value": the
           nearest, as the target's conversion instructions round */
        double d;
        if (fundamental_type_is_signed(b, from))
            d = static_cast<double>(static_cast<int64_t>(v));
        else
            d = static_cast<double>(static_cast<uint64_t>(v));
        if (to == fundamental_type::float_) {
            float f;
            if (fundamental_type_is_signed(b, from))
                f = static_cast<float>(static_cast<int64_t>(v));
            else
                f = static_cast<float>(static_cast<uint64_t>(v));
            d = f;
        }
        m.value = floating_kept(d);
        return;
    }
    double d = floating_value(m.value);
    if (to == fundamental_type::bool_) {
        /* [conv.bool]/1 */
        m.value = integer_wrap(b, to, d != 0);
        return;
    }
    if (fundamental_type_is_integral(to)) {
        /* [conv.fpint]/1: "The conversion truncates; that is, the
           fractional part is discarded.  The behavior is undefined if the
           truncated value cannot be represented in the destination type." */
        double t = trunc(d);
        if (!(t > -18446744073709551616.0 && t < 18446744073709551616.0) ||
            !integer_holds(b, to, static_cast<__int128>(t))) {
            m.evaluation = constness::not_constant;
            return;
        }
        if (!integer_kept_exactly(
                b, to,
                static_cast<unsigned __int128>(static_cast<__int128>(t)))) {
            m.evaluation = constness::unknown;
            return;
        }
        m.value = integer_wrap(
            b, to, static_cast<unsigned __int128>(static_cast<__int128>(t)));
        return;
    }
    if (to == fundamental_type::float_) {
        /* [conv.double]/1: "If the source value can be exactly represented
           in the destination type, the result of the conversion is that
           exact representation.  If the source value is between two
           adjacent destination values, the result of the conversion is an
           implementation-defined choice of either of those values.
           Otherwise, the behavior is undefined." */
        float f = static_cast<float>(d);
        if (isinf(f) && !isinf(d)) {
            m.evaluation = constness::not_constant;
            return;
        }
        m.value = floating_kept(f);
    }
}

/*
 * [expr.arith.conv]/1: "Many binary operators that expect operands of
 * arithmetic or enumeration type cause conversions and yield result types
 * in a similar way.  The purpose is to yield a common type, which is also
 * the type of the result."
 */
fundamental_type
semantic_analysis::usual_arithmetic_conversions(expression_meaning &a,
                                                expression_meaning &b)
{
    const implementation_defined_behavior &behavior = tokens.files.behavior;
    fundamental_type x = fundamental_of(a.type), y = fundamental_of(b.type);
    fundamental_type common = fundamental_type::none;
    /* "If either operand is of type long double, the other shall be
       converted to long double.  Otherwise, if either operand is double,
       the other shall be converted to double.  Otherwise, if either
       operand is float, the other shall be converted to float." */
    if (x == fundamental_type::long_double ||
        y == fundamental_type::long_double)
        common = fundamental_type::long_double;
    else if (x == fundamental_type::double_ || y == fundamental_type::double_)
        common = fundamental_type::double_;
    else if (x == fundamental_type::float_ || y == fundamental_type::float_)
        common = fundamental_type::float_;
    if (common == fundamental_type::none) {
        /* "Otherwise, the integral promotions shall be performed on both
           operands." */
        promote(a);
        promote(b);
        x = fundamental_of(a.type);
        y = fundamental_of(b.type);
        bool xs = fundamental_type_is_signed(behavior, x);
        bool ys = fundamental_type_is_signed(behavior, y);
        if (x == y) {
            common = x;
        } else if (xs == ys) {
            /* "the operand with the type of lesser integer conversion rank
               shall be converted to the type of the operand with greater
               rank" */
            common = x;
            if (integer_rank(y) > integer_rank(x))
                common = y;
        } else {
            fundamental_type u = x, s = y;
            if (xs) {
                u = y;
                s = x;
            }
            if (integer_rank(u) >= integer_rank(s))
                common = u;
            else if (fundamental_type_width(behavior, s) >
                     fundamental_type_width(behavior, u))
                common = s;
            else
                common = unsigned_of(s);
        }
    }
    convert_arithmetic(a, common);
    convert_arithmetic(b, common);
    return common;
}

/*
 * [conv.qual]/2: "Two types T1 and T2 are similar if they have
 * cv-decompositions with the same n such that corresponding P_i components
 * are either the same or one is "array of N_i" and the other is "array of
 * unknown bound of", and the types denoted by U are the same."
 */
bool semantic_analysis::similar(qualified_type a, qualified_type b) const
{
    if (a.unqualified() == b.unqualified())
        return true;
    const struct type x = type_table[a.unqualified()];
    const struct type y = type_table[b.unqualified()];
    if (x.kind == type_kind::pointer && y.kind == type_kind::pointer)
        return similar(x.operand, y.operand);
    if (x.kind == type_kind::array && y.kind == type_kind::array) {
        if (x.bound_known && y.bound_known && x.bound != y.bound)
            return false;
        return similar(x.operand, y.operand);
    }
    return false;
}

/*
 * [conv.qual]/3: "A prvalue of type T1 can be converted to type T2 if the
 * cv-combined type of T1 and T2 is T2."  The cv-combined type T3 has, at
 * each level j > 0, the union of the cv-qualifiers; an array of unknown
 * bound where either has one; and "if cv3_j or P3_j is different from
 * cv1_j or P1_j, then const is added to every cv3_k for 0 < k < j".  An
 * array's cv-qualifiers are its elements' ([basic.type.qualifier]/3).
 */
bool semantic_analysis::destroyed_temporary(const expression_meaning &m) const
{
    const struct type t = type_table[m.type.unqualified()];
    /* (a call's result is destroyed by lowering: its temporary is the
       call's own) */
    if (m.node && semantics[m.node].kind == semantic_kind::call)
        return false;
    return m.category == value_category::prvalue &&
           t.kind == type_kind::class_type &&
           entity_table[t.entity].kind == entity_kind::class_type &&
           entity_table[t.entity].destructor;
}

bool semantic_analysis::differ_in_exceptions(qualified_type a,
                                             qualified_type b) const
{
    const struct type x = type_table[a.unqualified()];
    const struct type y = type_table[b.unqualified()];
    if (x.kind != type_kind::function || y.kind != type_kind::function ||
        x.non_throwing == y.non_throwing || !(x.operand == y.operand) ||
        x.variadic != y.variadic || x.parameter_count != y.parameter_count ||
        a.cv() != b.cv())
        return false;
    const qualified_type *xp = type_table.parameters(a.unqualified());
    const qualified_type *yp = type_table.parameters(b.unqualified());
    for (uint32_t i = 0; i < x.parameter_count; i++)
        if (!(xp[i] == yp[i]))
            return false;
    return true;
}

bool semantic_analysis::function_pointer_convertible(qualified_type from,
                                                     qualified_type to) const
{
    /* [conv.fctptr]/1: "A prvalue of type "pointer to noexcept function"
       can be converted to a prvalue of type "pointer to function".  The
       result is a pointer to the function." */
    const struct type f0 = type_table[from.unqualified()];
    const struct type t0 = type_table[to.unqualified()];
    if (f0.kind != type_kind::pointer || t0.kind != type_kind::pointer)
        return false;
    return type_table[f0.operand.unqualified()].non_throwing &&
           differ_in_exceptions(f0.operand, t0.operand);
}

bool semantic_analysis::qualification_convertible(qualified_type from,
                                                  qualified_type to) const
{
    const struct type f0 = type_table[from.unqualified()];
    const struct type t0 = type_table[to.unqualified()];
    if (f0.kind != type_kind::pointer || t0.kind != type_kind::pointer)
        return false;
    qualified_type f = f0.operand, t = t0.operand;
    bool const_above = true;
    for (;;) {
        uint32_t cvf = f.cv(), cvt = t.cv();
        for (qualified_type e = f;
             type_table[e.unqualified()].kind == type_kind::array;) {
            e = type_table[e.unqualified()].operand;
            cvf |= e.cv();
        }
        for (qualified_type e = t;
             type_table[e.unqualified()].kind == type_kind::array;) {
            e = type_table[e.unqualified()].operand;
            cvt |= e.cv();
        }
        if (cvf & ~cvt)
            return false;
        const struct type pf = type_table[f.unqualified()];
        const struct type pt = type_table[t.unqualified()];
        bool level_differs = cvf != cvt, leaf = false;
        if (pf.kind == type_kind::pointer && pt.kind == type_kind::pointer) {
        } else if (pf.kind == type_kind::array && pt.kind == type_kind::array) {
            if (pf.bound_known && pt.bound_known && pf.bound != pt.bound)
                return false;
            if (!pf.bound_known && pt.bound_known)
                return false;
            if (pf.bound_known != pt.bound_known)
                level_differs = true;
        } else {
            if (f.unqualified() != t.unqualified())
                return false;
            leaf = true;
        }
        if (level_differs && !const_above)
            return false;
        if (leaf)
            return true;
        const_above = const_above && (cvt & cv_const);
        f = pf.operand;
        t = pt.operand;
    }
}

/*
 * [dcl.init]/17.9: "A standard conversion sequence will be used, if
 * necessary, to convert the initializer expression to the cv-unqualified
 * version of the destination type; no user-defined conversions are
 * considered.  If the conversion cannot be done, the initialization is
 * ill-formed."  [conv]/3: "An expression E can be implicitly converted to
 * a type T if and only if the declaration T t=E; is well-formed": the
 * other contexts of an implicit conversion use this one.
 */
analysis_result semantic_analysis::implicit_conversion(node_index at,
                                                       expression_meaning &m,
                                                       qualified_type to,
                                                       bool direct)
{
    const char *rule = "a standard conversion sequence converts the "
                       "initializer expression to the cv-unqualified "
                       "destination type, else the initialization is "
                       "ill-formed";
    to = qualified_type::make(to.unqualified(), cv_none);
    const struct type target = type_table[to.unqualified()];
    if (type_table[m.type.unqualified()].kind == type_kind::enumeration ||
        target.kind == type_kind::enumeration) {
        if (m.type.unqualified() == to.unqualified()) {
            m.type = to;
            return analysis_result::well_formed;
        }
        entity_index e = enumeration_of(m.type);
        if (target.kind == type_kind::enumeration || !e) {
            if (!enumeration_of(to) && target.kind == type_kind::enumeration)
                return not_supported(at, "dcl.enum");
            if (!e &&
                type_table[m.type.unqualified()].kind == type_kind::enumeration)
                return not_supported(at, "dcl.enum");
            /* [conv.integral]/1: "A prvalue of an integer type can be
               converted to a prvalue of another integer type.": an
               enumeration is none; no standard conversion makes one */
            if (fundamental_of(m.type) == fundamental_type::none &&
                !is_pointer(m.type) &&
                type_table[m.type.unqualified()].kind != type_kind::enumeration)
                return not_supported(at, "conv");
            return ill_formed(at, rule, "dcl.init");
        }
        if (target.kind != type_kind::fundamental)
            return not_supported(at, "conv");
        /* [conv.integral]/1: "A prvalue of an unscoped enumeration type
           can be converted to a prvalue of an integer type."; [conv.bool],
           [conv.fpint]/2 likewise.  A scoped one converts implicitly to
           nothing ([dcl.enum]/10) */
        if (entity_table[e].scoped)
            return ill_formed(at, rule, "dcl.init");
        enumeration_as_underlying(m);
    }
    fundamental_type from = fundamental_of(m.type);
    if (target.kind == type_kind::fundamental) {
        fundamental_type t = target.fundamental;
        if (fundamental_type_is_integral(t) ||
            fundamental_type_is_floating(t)) {
            if (is_arithmetic(m.type)) {
                convert_arithmetic(m, t);
                return analysis_result::well_formed;
            }
            if (t == fundamental_type::bool_ && is_pointer(m.type)) {
                /* [conv.bool]/1: "A prvalue of arithmetic, unscoped
                   enumeration, pointer, or pointer-to-member type can be
                   converted to a prvalue of type bool." */
                m.type = to;
                if (m.evaluation == constness::constant)
                    m.evaluation = constness::unknown;
                converted(m, conversion_kind::boolean);
                return analysis_result::well_formed;
            }
            if (t == fundamental_type::bool_ &&
                from == fundamental_type::std_nullptr_t && direct) {
                /* [dcl.init]/17.8: "if the initialization is
                   direct-initialization, the source type is std::nullptr_t,
                   and the destination type is bool, the initial value of
                   the object being initialized is false" */
                m.type = to;
                m.value = 0;
                converted(m, conversion_kind::boolean);
                return analysis_result::well_formed;
            }
            if (from == fundamental_type::none &&
                type_table[m.type.unqualified()].kind != type_kind::pointer)
                return not_supported(at, "conv");
            return ill_formed(at, rule, "dcl.init");
        }
        if (t == fundamental_type::std_nullptr_t) {
            /* [conv.ptr]/1: "A null pointer constant of integral type can
               be converted to a prvalue of type std::nullptr_t." */
            if (from == fundamental_type::std_nullptr_t || m.zero_literal) {
                bool same = from == fundamental_type::std_nullptr_t;
                m.type = to;
                m.zero_literal = false;
                if (!same)
                    converted(m, conversion_kind::null_pointer);
                return analysis_result::well_formed;
            }
            if (is_integral(m.type) && m.evaluation != constness::not_constant)
                return not_supported(at, "conv.ptr");
            if (from == fundamental_type::none &&
                type_table[m.type.unqualified()].kind != type_kind::pointer)
                return not_supported(at, "conv");
            return ill_formed(at, rule, "dcl.init");
        }
        return not_supported(at, "conv");
    }
    if (target.kind != type_kind::pointer)
        return not_supported(at, "conv");
    /* [conv.ptr]/1: "A null pointer constant is an integer literal with
       value zero or a prvalue of type std::nullptr_t.  A null pointer
       constant can be converted to a pointer type" */
    if (m.zero_literal || from == fundamental_type::std_nullptr_t) {
        m.type = to;
        m.zero_literal = false;
        m.value = 0;
        if (m.evaluation == constness::unknown)
            m.evaluation = constness::constant;
        converted(m, conversion_kind::null_pointer);
        return analysis_result::well_formed;
    }
    if (is_pointer(m.type)) {
        qualified_type pointee = type_table[m.type.unqualified()].operand;
        qualified_type target_pointee = target.operand;
        bool converts = m.type.unqualified() == to.unqualified() ||
                        qualification_convertible(m.type, to) ||
                        function_pointer_convertible(m.type, to);
        /* [conv.ptr]/2: "A prvalue of type "pointer to cv T", where T is
           an object type, can be converted to a prvalue of type "pointer
           to cv void"", then a qualification conversion */
        const struct type tp = type_table[target_pointee.unqualified()];
        if (!converts && tp.kind == type_kind::fundamental &&
            tp.fundamental == fundamental_type::void_ &&
            is_object_pointer(m.type) && !(pointee.cv() & ~target_pointee.cv()))
            converts = true;
        /* [diff.lex]/3: "String literals made const. ... char* p =
           "abc"; // valid in C, invalid in C++": C++03's deprecated
           conversion, clang's extension since C++11 */
        if (!converts && m.string_literal &&
            pointee.unqualified() == target_pointee.unqualified() &&
            !target_pointee.cv()) {
            if (tokens.files.behavior.has(
                    language_feature::list_initialization)) {
                analysis_result r =
                    extension_used(diagnostic_id::sema_writable_string,
                                   m.string_literal, nullptr);
                if (r != analysis_result::well_formed)
                    return r;
            }
            converts = true;
        }
        if (!converts) {
            const struct type fp = type_table[pointee.unqualified()];
            bool ambiguous = false;
            if (fp.kind == type_kind::class_type &&
                tp.kind == type_kind::class_type &&
                entity_table[fp.entity].size &&
                !(pointee.cv() & ~target_pointee.cv()) &&
                base_step(fp.entity, tp.entity, ambiguous))
                return pointer_to_base(tree[at].first_token, fp.entity,
                                       tp.entity, to, m);
            /* a function pointer conversion: not analysed yet */
            if (fp.kind == type_kind::class_type ||
                fp.kind == type_kind::function ||
                tp.kind == type_kind::class_type ||
                tp.kind == type_kind::function ||
                fp.kind == type_kind::enumeration ||
                tp.kind == type_kind::enumeration)
                return not_supported(at, "conv.ptr");
            return ill_formed(at, rule, "dcl.init");
        }
        bool same = m.type == to;
        m.type = to;
        if (!same)
            converted(m, conversion_kind::pointer);
        return analysis_result::well_formed;
    }
    /* [conv.ptr]/1: "A null pointer constant is an integer literal with
       value zero or a prvalue of type std::nullptr_t": a constant that is
       not zero is none, in any revision */
    if (is_integral(m.type) && m.evaluation == constness::constant &&
        m.value != 0)
        return ill_formed(at, rule, "dcl.init");
    if (is_integral(m.type) && m.evaluation != constness::not_constant)
        /* an integral constant expression with value zero: a null pointer
           constant in C++03, clang's extension after */
        return not_supported(at, "conv.ptr");
    if (from == fundamental_type::none)
        return not_supported(at, "conv");
    return ill_formed(at, rule, "dcl.init");
}

/*
 * [conv]/4: "An expression E appearing in such a context is said to be
 * contextually converted to bool and is well-formed if and only if the
 * declaration bool t(E); is well-formed"
 */
analysis_result semantic_analysis::contextual_bool(node_index at,
                                                   expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    if (type_table[m.type.unqualified()].kind == type_kind::class_type) {
        /* bool t(E): direct-initialization, by a conversion function
           ([dcl.init]/17.7) */
        bool converted = false;
        r = conversion_function_call(
            at, m,
            qualified_type::make(
                type_table.fundamental(fundamental_type::bool_), cv_none),
            true, converted);
        if (r != analysis_result::well_formed)
            return r;
    }
    r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    return implicit_conversion(
        at, m,
        qualified_type::make(type_table.fundamental(fundamental_type::bool_),
                             cv_none),
        true);
}

} // namespace cplusplus
