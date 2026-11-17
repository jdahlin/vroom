/*
 * expr.compound.cpp: [expr.compound] Compound expressions: the binary
 * operators of [expr.mptr.oper] to [expr.log.or], each subclause one
 * production of the same form, and [expr.comma] and [expr.const], which are
 * too small for a file of their own.
 *
 *     pm-expression:
 *         cast-expression
 *         pm-expression .* cast-expression
 *         pm-expression ->* cast-expression
 *
 *     multiplicative-expression:
 *         pm-expression
 *         multiplicative-expression * pm-expression
 *         multiplicative-expression / pm-expression
 *         multiplicative-expression % pm-expression
 *
 *     additive-expression:
 *         multiplicative-expression
 *         additive-expression + multiplicative-expression
 *         additive-expression - multiplicative-expression
 *
 *     shift-expression:
 *         additive-expression
 *         shift-expression << additive-expression
 *         shift-expression >> additive-expression
 *
 *     compare-expression:
 *         shift-expression
 *         compare-expression <=> shift-expression
 *
 *     relational-expression:
 *         compare-expression
 *         relational-expression < compare-expression
 *         relational-expression > compare-expression
 *         relational-expression <= compare-expression
 *         relational-expression >= compare-expression
 *
 *     equality-expression:
 *         relational-expression
 *         equality-expression == relational-expression
 *         equality-expression != relational-expression
 *
 *     and-expression:
 *         equality-expression
 *         and-expression & equality-expression
 *
 *     exclusive-or-expression:
 *         and-expression
 *         exclusive-or-expression ^ and-expression
 *
 *     inclusive-or-expression:
 *         exclusive-or-expression
 *         inclusive-or-expression | exclusive-or-expression
 *
 *     logical-and-expression:
 *         inclusive-or-expression
 *         logical-and-expression && inclusive-or-expression
 *
 *     logical-or-expression:
 *         logical-and-expression
 *         logical-or-expression || logical-and-expression
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

#include <math.h>

namespace cplusplus
{

namespace
{

/*
 * The twelve productions as a table: each nonterminal is its own operators
 * between operands of the next row's nonterminal, and the first alternative
 * passes that operand on.  A row's index is its level: logical-or-expression
 * is the lowest, pm-expression the highest, and above it is cast-expression.
 */
struct binary_production {
    syntax_kind kind;
    preprocessing_op_or_punc operators[4];
    unsigned operator_count;
};

constexpr binary_production binary_productions[] = {
    {syntax_kind::logical_or_expression,
     {preprocessing_op_or_punc::BAR_BAR},
     1},
    {syntax_kind::logical_and_expression,
     {preprocessing_op_or_punc::AMPERSAND_AMPERSAND},
     1},
    {syntax_kind::inclusive_or_expression, {preprocessing_op_or_punc::BAR}, 1},
    {syntax_kind::exclusive_or_expression,
     {preprocessing_op_or_punc::CARET},
     1},
    {syntax_kind::and_expression, {preprocessing_op_or_punc::AMPERSAND}, 1},
    {syntax_kind::equality_expression,
     {preprocessing_op_or_punc::EQUAL_EQUAL,
      preprocessing_op_or_punc::EXCLAMATION_EQUAL},
     2},
    {syntax_kind::relational_expression,
     {preprocessing_op_or_punc::LESS, preprocessing_op_or_punc::GREATER,
      preprocessing_op_or_punc::LESS_EQUAL,
      preprocessing_op_or_punc::GREATER_EQUAL},
     4},
    {syntax_kind::compare_expression, {preprocessing_op_or_punc::SPACESHIP}, 1},
    {syntax_kind::shift_expression,
     {preprocessing_op_or_punc::LESS_LESS,
      preprocessing_op_or_punc::GREATER_GREATER},
     2},
    {syntax_kind::additive_expression,
     {preprocessing_op_or_punc::PLUS, preprocessing_op_or_punc::MINUS},
     2},
    {syntax_kind::multiplicative_expression,
     {preprocessing_op_or_punc::STAR, preprocessing_op_or_punc::SLASH,
      preprocessing_op_or_punc::PERCENT},
     3},
    {syntax_kind::pm_expression,
     {preprocessing_op_or_punc::DOT_STAR, preprocessing_op_or_punc::ARROW_STAR},
     2},
};
constexpr unsigned binary_production_count =
    sizeof binary_productions / sizeof binary_productions[0];

/* the level whose operator the token is; binary_production_count if it is none */
unsigned level_of_operator(const token &t);

} // namespace

bool parser::binary_operator_follows()
{
    return level_of_operator(peek()) != binary_production_count;
}

namespace
{

/* the level whose operator the token is; binary_production_count if it is none */
unsigned level_of_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return binary_production_count;
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(t.op_or_punc);
    for (unsigned level = 0; level < binary_production_count; level++)
        for (unsigned i = 0; i < binary_productions[level].operator_count; i++)
            if (binary_productions[level].operators[i] == op)
                return level;
    return binary_production_count;
}

} // namespace

/*
 * The nonterminal of lowest_level: operands of the next level between its
 * operators, grouped as the left-recursive productions say ([expr.mul]/1:
 * "The multiplicative operators *, /, and % group left-to-right", and the
 * same sentence in each of the other subclauses).  An operand that is
 * followed by an operator of a higher level is first the left operand of
 * that one.  With no operator, the cast-expression passes through.
 *
 * A '>' is the greater-than operator except where it ends a
 * template-argument-list ([temp.names]/3: greater_is_operator).  A
 * fold-operator before '...' is only in a fold-expression's parentheses
 * ([expr.prim.fold]).
 */
node_index parser::binary_expression(unsigned lowest_level)
{
    size_t mark = begin_node();
    token_index first = position;
    node_index n = cast_expression();
    if (failed())
        return 0;
    for (;;) {
        unsigned level = level_of_operator(peek());
        if (level == binary_production_count || level < lowest_level)
            break;
        /* [temp.names]/3: in a template-argument-list the first
           non-nested > ends it */
        if (!greater_is_operator && at_closing_angle())
            break;
        /* [expr.prim.fold]/1: a fold-expression is parenthesized */
        if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
            return expected("'(' around the fold-expression", "expr.prim.fold");
        add_token();
        binary_expression(level + 1);
        if (failed())
            return 0;
        n = finish_node(binary_productions[level].kind, mark, first);
    }
    return n;
}

/*
 * [expr.comma]:
 *
 *     expression:
 *         assignment-expression
 *         expression , assignment-expression
 *
 * [expr.comma]/1: "The comma operator groups left-to-right."
 */
node_index parser::expression()
{
    size_t mark = begin_node();
    token_index first = position;
    node_index n = assignment_expression();
    if (failed())
        return 0;
    while (at(preprocessing_op_or_punc::COMMA)) {
        /* , ... ends a pack's list (an initializer-list, a
           template-argument-list): its caller's */
        if (at(preprocessing_op_or_punc::ELLIPSIS, 1))
            break;
        add_token();
        assignment_expression();
        if (failed())
            return 0;
        n = finish_node(syntax_kind::expression, mark, first);
    }
    return n;
}

/*
 * [expr.const]:
 *
 *     constant-expression:
 *         conditional-expression
 *
 * [expr.const]/1: "Certain contexts require expressions that satisfy
 * additional requirements as detailed in this subclause": the requirements
 * are semantic; the grammar only leaves out the assignment and comma
 * operators.
 */
node_index parser::constant_expression()
{
    return conditional_expression();
}

/* ---- semantic analysis ---------------------------------------------- */

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

} // namespace

/*
 * The operators of [expr.mul], [expr.add] (on arithmetic operands),
 * [expr.shift], [expr.bit.and], [expr.xor] and [expr.or]: the operands are
 * prvalues; the result is a prvalue of their common type.
 */
analysis_result semantic_analysis::arithmetic(node_index n,
                                              preprocessing_op_or_punc op,
                                              expression_meaning &left,
                                              expression_meaning &right,
                                              expression_meaning &m)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool integral_only = op != preprocessing_op_or_punc::STAR &&
                         op != preprocessing_op_or_punc::SLASH &&
                         op != preprocessing_op_or_punc::PLUS &&
                         op != preprocessing_op_or_punc::MINUS;
    bool allowed = is_arithmetic(left.type) && is_arithmetic(right.type);
    if (integral_only)
        allowed = is_integral(left.type) && is_integral(right.type);
    if (!allowed) {
        auto known = [&](const expression_meaning &x) {
            return fundamental_of(x.type) != fundamental_type::none ||
                   is_pointer(x.type);
        };
        if (!known(left) || !known(right))
            return not_supported(n, "expr.compound");
        switch (op) {
        case preprocessing_op_or_punc::STAR:
        case preprocessing_op_or_punc::SLASH:
        case preprocessing_op_or_punc::PERCENT:
            /* [expr.mul]/2 */
            return ill_formed(n,
                              "the operands of * and / shall have arithmetic "
                              "or unscoped enumeration type; the operands of "
                              "% shall have integral or unscoped enumeration "
                              "type",
                              "expr.mul");
        case preprocessing_op_or_punc::LESS_LESS:
        case preprocessing_op_or_punc::GREATER_GREATER:
            /* [expr.shift]/1 */
            return ill_formed(n,
                              "the operands shall be of integral or unscoped "
                              "enumeration type",
                              "expr.shift");
        case preprocessing_op_or_punc::AMPERSAND:
            return ill_formed(n,
                              "the operands shall be of integral or unscoped "
                              "enumeration type",
                              "expr.bit.and");
        case preprocessing_op_or_punc::CARET:
            return ill_formed(n,
                              "the operands shall be of integral or unscoped "
                              "enumeration type",
                              "expr.xor");
        case preprocessing_op_or_punc::BAR:
            return ill_formed(n,
                              "the operands shall be of integral or unscoped "
                              "enumeration type",
                              "expr.or");
        default:
            /* [expr.add]/1: "For addition, either both operands shall have
               arithmetic or unscoped enumeration type, or one operand shall
               be a pointer to a completely-defined object type and the
               other shall have integral or unscoped enumeration type." */
            return ill_formed(n,
                              "both operands shall have arithmetic or "
                              "unscoped enumeration type, or one shall be a "
                              "pointer to a completely-defined object type "
                              "and the other integral",
                              "expr.add");
        }
    }
    fundamental_type t;
    bool shift = op == preprocessing_op_or_punc::LESS_LESS ||
                 op == preprocessing_op_or_punc::GREATER_GREATER;
    if (shift) {
        /* [expr.shift]/1: "The operands shall be of integral or unscoped
           enumeration type and integral promotions are performed.  The type
           of the result is that of the promoted left operand." */
        promote(left);
        promote(right);
        t = fundamental_of(left.type);
    } else {
        t = usual_arithmetic_conversions(left, right);
    }
    m = expression_meaning();
    m.type = left.type;
    m.category = value_category::prvalue;
    m.beyond_integral_constant =
        left.beyond_integral_constant || right.beyond_integral_constant;
    m.evaluation = combined_constness(left.evaluation, right.evaluation);
    if (m.evaluation != constness::constant)
        return analysis_result::well_formed;
    if (fundamental_type_is_floating(t)) {
        /* the rounding mode a pragma sets ([cfenv.syn], C's 7.6.1) */
        if (t == fundamental_type::long_double ||
            tokens.files.floating_point_pragma) {
            m.evaluation = constness::unknown;
            return analysis_result::well_formed;
        }
        double x = floating_value(left.value), y = floating_value(right.value);
        double r = floating_operation(t, op, x, y);
        /* [expr.pre]/4: "If during the evaluation of an expression, the
           result is not mathematically defined or not in the range of
           representable values for its type, the behavior is undefined." */
        if ((op == preprocessing_op_or_punc::SLASH && y == 0) || isinf(r) ||
            isnan(r)) {
            m.evaluation = constness::not_constant;
            return analysis_result::well_formed;
        }
        m.value = floating_kept(r);
        return analysis_result::well_formed;
    }
    /* (a 128-bit operation is not evaluated here: integer_kept_exactly) */
    if (fundamental_type_width(b, t) >= 128) {
        m.evaluation = constness::unknown;
        return analysis_result::well_formed;
    }
    bool is_signed = fundamental_type_is_signed(b, t);
    __int128 x = integer_value(b, t, left.value);
    __int128 y = integer_value(b, fundamental_of(right.type), right.value);
    unsigned width = fundamental_type_width(b, t);
    __int128 r = 0;
    bool undefined = false;
    switch (op) {
    case preprocessing_op_or_punc::PLUS:
        r = x + y;
        break;
    case preprocessing_op_or_punc::MINUS:
        r = x - y;
        break;
    case preprocessing_op_or_punc::STAR:
        if (is_signed)
            r = x * y;
        else
            r = static_cast<__int128>(static_cast<unsigned __int128>(x) *
                                          static_cast<unsigned __int128>(y) &
                                      ~uint64_t(0));
        break;
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
        /* [expr.mul]/4: "If the second operand of / or % is zero the
           behavior is undefined. ... if the quotient a/b is representable
           in the type of the result, (a/b)*b + a%b is equal to a; otherwise,
           the behavior of both a/b and a%b is undefined." */
        if (y == 0) {
            undefined = true;
            break;
        }
        if (op == preprocessing_op_or_punc::SLASH)
            r = x / y;
        else
            r = x % y;
        if (is_signed && !integer_holds(b, t, x / y))
            undefined = true;
        break;
    case preprocessing_op_or_punc::AMPERSAND:
        r = x & y;
        break;
    case preprocessing_op_or_punc::CARET:
        r = x ^ y;
        break;
    case preprocessing_op_or_punc::BAR:
        r = x | y;
        break;
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
        /* [expr.shift]/1: "The behavior is undefined if the right operand
           is negative, or greater than or equal to the width of the
           promoted left operand." */
        if (y < 0 || y >= width) {
            undefined = true;
            break;
        }
        if (op == preprocessing_op_or_punc::GREATER_GREATER) {
            /* [expr.shift]/3: "The value of E1 >> E2 is E1/2^E2, rounded
               down." */
            r = x >> static_cast<int>(y);
            break;
        }
        /* [expr.shift]/2: "The value of E1 << E2 is the unique value
           congruent to E1 x 2^E2 modulo 2^N" (C++20); before, for a signed
           E1, "if E1 has a signed type and non-negative value, and
           E1 x 2^E2 is representable in the corresponding unsigned type of
           the result type, then that value, converted to the result type,
           is the resulting value; otherwise, the behavior is undefined"
           (C++17 [expr.shift]/2, CWG1457) */
        r = static_cast<__int128>(static_cast<unsigned __int128>(x)
                                  << static_cast<int>(y));
        if (is_signed && !b.has(language_feature::modular_left_shift) &&
            (x < 0 || (r >> width) != 0))
            undefined = true;
        break;
    default:
        return not_supported(n, "expr.compound");
    }
    if (!undefined && is_signed && !shift && !integer_holds(b, t, r))
        /* signed overflow: [expr.pre]/4 */
        undefined = true;
    if (undefined) {
        m.evaluation = constness::not_constant;
        return analysis_result::well_formed;
    }
    m.value = integer_wrap(b, t, static_cast<unsigned __int128>(r));
    return analysis_result::well_formed;
}

/*
 * [expr.type]/3: "The composite pointer type of two operands p1 and p2
 * having types T1 and T2, respectively, where at least one is a pointer or
 * pointer-to-member type or std::nullptr_t, is:
 *   - if both p1 and p2 are null pointer constants, std::nullptr_t;
 *   - if either p1 or p2 is a null pointer constant, T2 or T1,
 *     respectively;
 *   - if T1 or T2 is "pointer to cv1 void" and the other type is "pointer
 *     to cv2 T", where T is an object type or void, "pointer to cv12
 *     void", where cv12 is the union of cv1 and cv2;
 *   ...
 *   - if T1 and T2 are similar types, the cv-combined type of T1 and T2;
 *   - otherwise, a program that necessitates the determination of a
 *     composite pointer type is ill-formed."
 * Analysis writes the cv-combined type of one level of pointers.
 */
analysis_result semantic_analysis::composite_pointer_type(node_index at,
                                                          expression_meaning &a,
                                                          expression_meaning &b)
{
    auto null = [&](const expression_meaning &x) {
        return x.zero_literal ||
               fundamental_of(x.type) == fundamental_type::std_nullptr_t;
    };
    qualified_type composite;
    if (null(a) && null(b)) {
        composite = qualified_type::make(
            type_table.fundamental(fundamental_type::std_nullptr_t), cv_none);
    } else if (null(a) && is_pointer(b.type)) {
        composite = b.type;
    } else if (null(b) && is_pointer(a.type)) {
        composite = a.type;
    } else if (is_pointer(a.type) && is_pointer(b.type)) {
        qualified_type x = type_table[a.type.unqualified()].operand;
        qualified_type y = type_table[b.type.unqualified()].operand;
        bool x_void = fundamental_of(x) == fundamental_type::void_;
        bool y_void = fundamental_of(y) == fundamental_type::void_;
        if (a.type.unqualified() == b.type.unqualified()) {
            composite = a.type;
        } else if ((x_void && (is_object_pointer(b.type) || y_void)) ||
                   (y_void && is_object_pointer(a.type))) {
            qualified_type v = qualified_type::make(
                type_table.fundamental(fundamental_type::void_),
                x.cv() | y.cv());
            composite = qualified_type::make(type_table.pointer_to(v), cv_none);
        } else if (x.unqualified() == y.unqualified() &&
                   type_table[x.unqualified()].kind != type_kind::array) {
            qualified_type c =
                qualified_type::make(x.unqualified(), x.cv() | y.cv());
            composite = qualified_type::make(type_table.pointer_to(c), cv_none);
        } else {
            return not_supported(at, "expr.type");
        }
    } else {
        return not_supported(at, "expr.type");
    }
    analysis_result r = implicit_conversion(at, a, composite, false);
    if (r == analysis_result::well_formed)
        r = implicit_conversion(at, b, composite, false);
    if (r == analysis_result::ill_formed)
        return not_supported(at, "expr.type");
    return r;
}

/*
 * [expr.rel] and [expr.eq]: the operands are prvalues; the result is a
 * bool.
 */
analysis_result semantic_analysis::comparison(node_index n,
                                              preprocessing_op_or_punc op,
                                              expression_meaning &left,
                                              expression_meaning &right,
                                              expression_meaning &m)
{
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool equality = op == preprocessing_op_or_punc::EQUAL_EQUAL ||
                    op == preprocessing_op_or_punc::EXCLAMATION_EQUAL;
    m = expression_meaning();
    m.type = qualified_type::make(
        type_table.fundamental(fundamental_type::bool_), cv_none);
    m.category = value_category::prvalue;
    m.beyond_integral_constant =
        left.beyond_integral_constant || right.beyond_integral_constant;
    if (is_arithmetic(left.type) && is_arithmetic(right.type)) {
        /* [expr.rel]/3, [expr.eq]/7: "the usual arithmetic conversions are
           performed"; [expr.rel]/6: "each of the operators shall yield true
           if the specified relationship is true and false if it is false" */
        fundamental_type t = usual_arithmetic_conversions(left, right);
        m.evaluation = combined_constness(left.evaluation, right.evaluation);
        if (m.evaluation != constness::constant)
            return analysis_result::well_formed;
        if (t == fundamental_type::long_double) {
            m.evaluation = constness::unknown;
            return analysis_result::well_formed;
        }
        bool less, equal;
        if (fundamental_type_is_floating(t)) {
            double x = floating_value(left.value),
                   y = floating_value(right.value);
            less = x < y;
            equal = x == y;
            if (x != x || y != y) {
                m.evaluation = constness::unknown;
                return analysis_result::well_formed;
            }
        } else {
            __int128 x = integer_value(b, t, left.value);
            __int128 y = integer_value(b, t, right.value);
            less = x < y;
            equal = x == y;
        }
        bool result = false;
        switch (op) {
        case preprocessing_op_or_punc::LESS:
            result = less;
            break;
        case preprocessing_op_or_punc::GREATER:
            result = !less && !equal;
            break;
        case preprocessing_op_or_punc::LESS_EQUAL:
            result = less || equal;
            break;
        case preprocessing_op_or_punc::GREATER_EQUAL:
            result = !less;
            break;
        case preprocessing_op_or_punc::EQUAL_EQUAL:
            result = equal;
            break;
        default:
            result = !equal;
            break;
        }
        m.value = result;
        return analysis_result::well_formed;
    }
    bool pointers = is_pointer(left.type) || is_pointer(right.type);
    bool nullptrs =
        fundamental_of(left.type) == fundamental_type::std_nullptr_t ||
        fundamental_of(right.type) == fundamental_type::std_nullptr_t;
    if (!pointers && !nullptrs) {
        if (fundamental_of(left.type) == fundamental_type::none ||
            fundamental_of(right.type) == fundamental_type::none)
            return not_supported(n, "expr.compound");
        if (equality)
            return ill_formed(n,
                              "the converted operands shall have arithmetic, "
                              "enumeration, pointer, or pointer-to-member "
                              "type, or type std::nullptr_t",
                              "expr.eq");
        return ill_formed(n,
                          "the converted operands shall have arithmetic, "
                          "enumeration, or pointer type",
                          "expr.rel");
    }
    if (!equality && !(is_pointer(left.type) && is_pointer(right.type)))
        /* a relational comparison with a null pointer constant */
        return not_supported(n, "expr.rel");
    analysis_result r = composite_pointer_type(n, left, right);
    if (r != analysis_result::well_formed)
        return r;
    m.evaluation = combined_constness(left.evaluation, right.evaluation);
    if (fundamental_of(left.type) == fundamental_type::std_nullptr_t &&
        m.evaluation == constness::constant) {
        /* [expr.eq]/5: "Two operands of type std::nullptr_t or one operand
           of type std::nullptr_t and the other a null pointer constant
           compare equal." */
        m.value = op == preprocessing_op_or_punc::EQUAL_EQUAL;
        return analysis_result::well_formed;
    }
    /* addresses are not modelled */
    if (m.evaluation == constness::constant)
        m.evaluation = constness::unknown;
    return analysis_result::well_formed;
}

/* a binary operation analysed (well-formed): its node, of the converted
   operands */
analysis_result semantic_analysis::binary_built(analysis_result r,
                                                token_index at,
                                                preprocessing_op_or_punc op,
                                                const expression_meaning &left,
                                                const expression_meaning &right,
                                                expression_meaning &m)
{
    if (r != analysis_result::well_formed)
        return r;
    m.node = expression_node(semantic_kind::binary, m, at, left.node,
                             right.node, static_cast<uint8_t>(op));
    return r;
}

/*
 * The binary operators of [expr.mul] to [expr.log.or]: E1 op E2.
 */
analysis_result semantic_analysis::binary(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 3 || !children[1].is_token())
        return not_supported(n, "expr.compound");
    const token &t = tokens[children[1].index()];
    if (t.kind != token_kind::operator_or_punctuator)
        return not_supported(n, "expr.compound");
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(t.op_or_punc);
    token_index at = children[1].index();
    expression_meaning left, right;
    analysis_result r = expression(children[0], left);
    if (r != analysis_result::well_formed)
        return r;
    r = expression(children[2], right);
    if (r != analysis_result::well_formed)
        return r;
    if (op == preprocessing_op_or_punc::AMPERSAND_AMPERSAND ||
        op == preprocessing_op_or_punc::BAR_BAR) {
        /* [expr.log.and]/1: "The operands are both contextually converted
           to bool.  The result is true if both operands are true and false
           otherwise. ... the second operand is not evaluated if the first
           operand is false."  [expr.log.or]/1 likewise: "the second operand
           is not evaluated if the first operand evaluates to true." */
        r = contextual_bool(n, left);
        if (r != analysis_result::well_formed)
            return r;
        r = contextual_bool(n, right);
        if (r != analysis_result::well_formed)
            return r;
        bool is_and = op == preprocessing_op_or_punc::AMPERSAND_AMPERSAND;
        m = expression_meaning();
        m.type = left.type;
        m.category = value_category::prvalue;
        m.beyond_integral_constant =
            left.beyond_integral_constant || right.beyond_integral_constant;
        if (left.evaluation == constness::constant &&
            (left.value != 0) != is_and) {
            m.evaluation = constness::constant;
            m.value = left.value;
        } else {
            m.evaluation =
                combined_constness(left.evaluation, right.evaluation);
            m.value = right.value;
        }
        semantic_kind kind = semantic_kind::logical_or;
        if (is_and)
            kind = semantic_kind::logical_and;
        m.node = expression_node(kind, m, at, left.node, right.node);
        return analysis_result::well_formed;
    }
    r = operand_value(n, left);
    if (r == analysis_result::well_formed)
        r = operand_value(n, right);
    if (r != analysis_result::well_formed)
        return r;
    /* [expr.arith.conv]/1, [expr.add]/1, [expr.shift]/1: unscoped
       enumeration operands are promoted; two of one scoped enumeration
       type are compared ([expr.rel]/3, [expr.eq]/7) */
    enumeration_operands(left, right,
                         op == preprocessing_op_or_punc::LESS ||
                             op == preprocessing_op_or_punc::GREATER ||
                             op == preprocessing_op_or_punc::LESS_EQUAL ||
                             op == preprocessing_op_or_punc::GREATER_EQUAL ||
                             op == preprocessing_op_or_punc::EQUAL_EQUAL ||
                             op == preprocessing_op_or_punc::EXCLAMATION_EQUAL);
    switch (op) {
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
        r = comparison(n, op, left, right, m);
        return binary_built(r, at, op, left, right, m);
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
        break;
    default:
        r = arithmetic(n, op, left, right, m);
        return binary_built(r, at, op, left, right, m);
    }
    bool lp = is_pointer(left.type), rp = is_pointer(right.type);
    if (!lp && !rp) {
        r = arithmetic(n, op, left, right, m);
        return binary_built(r, at, op, left, right, m);
    }
    /* [expr.add]: pointer arithmetic */
    auto complete_object = [&](const expression_meaning &x) {
        return is_object_pointer(x.type) &&
               size_of_type(type_table[x.type.unqualified()].operand) != 0;
    };
    const char *rule = "one operand shall be a pointer to a "
                       "completely-defined object type and the other shall "
                       "have integral or unscoped enumeration type";
    m = expression_meaning();
    m.category = value_category::prvalue;
    m.evaluation = combined_constness(left.evaluation, right.evaluation);
    if (m.evaluation == constness::constant)
        m.evaluation = constness::unknown;
    if (lp && rp) {
        /* [expr.add]/2: "For subtraction, one of the following shall hold:
           both operands have arithmetic or unscoped enumeration type; or
           both operands are pointers to cv-qualified or cv-unqualified
           versions of the same completely-defined object type; or the left
           operand is a pointer to a completely-defined object type and the
           right operand has integral or unscoped enumeration type." */
        qualified_type x = type_table[left.type.unqualified()].operand;
        qualified_type y = type_table[right.type.unqualified()].operand;
        if (op == preprocessing_op_or_punc::PLUS)
            return ill_formed(n, rule, "expr.add");
        if (!complete_object(left) || !complete_object(right))
            return not_supported(n, "expr.add");
        if (x.unqualified() != y.unqualified())
            return ill_formed(n,
                              "both operands shall be pointers to "
                              "cv-qualified or cv-unqualified versions of the "
                              "same completely-defined object type",
                              "expr.add");
        /* [expr.add]/5: std::ptrdiff_t, long on aarch64 Linux */
        m.type = qualified_type::make(
            type_table.fundamental(fundamental_type::long_int), cv_none);
        return binary_built(analysis_result::well_formed, at, op, left, right,
                            m);
    }
    expression_meaning *pointer_operand = &right, *integer_operand = &left;
    if (lp) {
        pointer_operand = &left;
        integer_operand = &right;
    }
    const expression_meaning &pointer = *pointer_operand;
    const expression_meaning &integer = *integer_operand;
    if (!is_integral(integer.type)) {
        if (fundamental_of(integer.type) == fundamental_type::none)
            return not_supported(n, "expr.add");
        return ill_formed(n, rule, "expr.add");
    }
    if (rp && op == preprocessing_op_or_punc::MINUS)
        return ill_formed(n, rule, "expr.add");
    if (!complete_object(pointer))
        /* void * and function pointers: GCC's extensions */
        return not_supported(n, "expr.add");
    /* [expr.add]/4: "When an expression J that has integral type is added
       to or subtracted from an expression P of pointer type, the result has
       the type of P." */
    m.type = pointer.type;
    return binary_built(analysis_result::well_formed, at, op, left, right, m);
}

/*
 * [expr.cond]/1: "The first expression is contextually converted to bool.
 * It is evaluated and if it is true, the result of the conditional
 * expression is the value of the second expression, otherwise that of the
 * third expression."
 */
analysis_result semantic_analysis::conditional(node_index n,
                                               expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 5)
        /* GCC's a ?: b */
        return not_supported(n, "expr.cond");
    expression_meaning c, e2, e3;
    analysis_result r = expression(children[0], c);
    if (r == analysis_result::well_formed)
        r = contextual_bool(n, c);
    if (r == analysis_result::well_formed)
        r = expression(children[2], e2);
    if (r == analysis_result::well_formed)
        r = expression(children[4], e3);
    if (r != analysis_result::well_formed)
        return r;
    if (fundamental_of(e2.type) == fundamental_type::void_ ||
        fundamental_of(e3.type) == fundamental_type::void_)
        return not_supported(n, "expr.cond");
    bool chosen_known = c.evaluation == constness::constant;
    expression_meaning *chosen_operand = &e3;
    if (c.value)
        chosen_operand = &e2;
    const expression_meaning &chosen = *chosen_operand;
    /* [expr.cond]/4: glvalues of the same value category and the same type
       except for cv-qualification: each converts to "lvalue reference to"
       the other's type if it binds directly, which needs the target's
       cv-qualifiers to include the source's */
    if (e2.category != value_category::prvalue && e2.category == e3.category &&
        e2.type.unqualified() == e3.type.unqualified()) {
        uint32_t cv2 = e2.type.cv(), cv3 = e3.type.cv();
        if (!(cv2 & ~cv3) || !(cv3 & ~cv2)) {
            /* [expr.cond]/5: "If the second and third operands are
               glvalues of the same value category and have the same type,
               the result is of that type and value category" */
            m = expression_meaning();
            m.type = qualified_type::make(e2.type.unqualified(), cv2 | cv3);
            m.category = e2.category;
            m.beyond_integral_constant = c.beyond_integral_constant ||
                                         e2.beyond_integral_constant ||
                                         e3.beyond_integral_constant;
            if (chosen_known) {
                m.evaluation = chosen.evaluation;
                m.entity = chosen.entity;
            } else {
                m.evaluation = combined_constness(
                    c.evaluation,
                    combined_constness(e2.evaluation, e3.evaluation));
                if (m.evaluation == constness::constant)
                    m.evaluation = constness::unknown;
            }
            m.node = expression_node(semantic_kind::conditional, m,
                                     tree[n].first_token, c.node, e2.node);
            semantics[m.node].c = e3.node;
            return analysis_result::well_formed;
        }
    }
    /* [expr.cond]/6: "Otherwise, the result is a prvalue."  /7:
       "Lvalue-to-rvalue, array-to-pointer, and function-to-pointer standard
       conversions are performed on the second and third operands." */
    r = operand_value(n, e2);
    if (r == analysis_result::well_formed)
        r = operand_value(n, e3);
    if (r != analysis_result::well_formed)
        return r;
    bool zero2 = e2.zero_literal, zero3 = e3.zero_literal;
    /* [expr.cond]/7.3: "The second and third operands have arithmetic or
       enumeration type; the usual arithmetic conversions are performed" */
    if (e2.type.unqualified() != e3.type.unqualified())
        enumeration_operands(e2, e3, false);
    if (e2.type.unqualified() == e3.type.unqualified()) {
        /* "The second and third operands have the same type; the result is
           of that type" */
    } else if (is_arithmetic(e2.type) && is_arithmetic(e3.type)) {
        /* "the usual arithmetic conversions are performed to bring them to
           a common type" */
        usual_arithmetic_conversions(e2, e3);
    } else if (is_pointer(e2.type) || is_pointer(e3.type) ||
               fundamental_of(e2.type) == fundamental_type::std_nullptr_t ||
               fundamental_of(e3.type) == fundamental_type::std_nullptr_t) {
        /* "pointer conversions, function pointer conversions, and
           qualification conversions are performed to bring them to their
           composite pointer type"; "Both the second and third operands have
           type std::nullptr_t or one has that type and the other is a null
           pointer constant." */
        if (!(is_pointer(e2.type) || zero2 ||
              fundamental_of(e2.type) == fundamental_type::std_nullptr_t) ||
            !(is_pointer(e3.type) || zero3 ||
              fundamental_of(e3.type) == fundamental_type::std_nullptr_t))
            return not_supported(n, "expr.cond");
        r = composite_pointer_type(n, e2, e3);
        if (r != analysis_result::well_formed)
            return r;
    } else {
        if (fundamental_of(e2.type) == fundamental_type::none ||
            fundamental_of(e3.type) == fundamental_type::none)
            return not_supported(n, "expr.cond");
        return ill_formed(n,
                          "after the conversions, the operands have the same "
                          "type, arithmetic types, pointer types or "
                          "std::nullptr_t",
                          "expr.cond");
    }
    m = expression_meaning();
    m.type = e2.type;
    m.category = value_category::prvalue;
    m.beyond_integral_constant = c.beyond_integral_constant ||
                                 e2.beyond_integral_constant ||
                                 e3.beyond_integral_constant;
    if (chosen_known) {
        m.evaluation = chosen.evaluation;
        m.value = chosen.value;
    } else {
        m.evaluation = combined_constness(
            c.evaluation, combined_constness(e2.evaluation, e3.evaluation));
        if (m.evaluation == constness::constant)
            m.evaluation = constness::unknown;
    }
    m.node = expression_node(semantic_kind::conditional, m, tree[n].first_token,
                             c.node, e2.node);
    semantics[m.node].c = e3.node;
    return analysis_result::well_formed;
}

/*
 * [expr.comma]/1: "A pair of expressions separated by a comma is evaluated
 * left-to-right; the left expression is a discarded-value expression. ...
 * The type and value of the result are the type and value of the right
 * operand; the result is of the same value category as its right operand".
 */
analysis_result semantic_analysis::comma(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    constness so_far = constness::constant;
    bool any = false;
    for (uint32_t i = 0; i < node.child_count; i++) {
        if (syntax_is_punctuator(tokens, children[i],
                                 preprocessing_op_or_punc::COMMA))
            continue;
        expression_meaning e;
        analysis_result r = expression(children[i], e);
        if (r != analysis_result::well_formed)
            return r;
        uint32_t previous = m.node;
        if (any)
            so_far = combined_constness(so_far, m.evaluation);
        m = e;
        /* the left operand is evaluated first ([expr.comma]/1) */
        if (any)
            m.node =
                expression_node(semantic_kind::comma, e,
                                first_token(children[i]), previous, e.node);
        any = true;
    }
    if (!any)
        return not_supported(n, "expr.comma");
    /* the left operands are evaluated: one that is no constant expression
       makes the whole none */
    if (so_far != constness::constant) {
        if (so_far == constness::not_constant ||
            m.evaluation != constness::not_constant)
            m.evaluation = so_far;
        m.entity = 0;
    }
    m.beyond_integral_constant = true;
    m.zero_literal = false;
    m.string_literal = 0;
    if (m.node && node.child_count > 1)
        semantics[m.node].evaluation = m.evaluation;
    return analysis_result::well_formed;
}

} // namespace cplusplus
