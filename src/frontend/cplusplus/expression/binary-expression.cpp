/*
 * binary-expression.cpp: [expr.compound] Compound expressions: the binary
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
           non-nested > ends it (and a >> or >>= from C++11 on); a >= is
           still the operator after an expression, [impl.clang] as clang takes
           it (only after a type-id is it split: accept_closing_angle) */
        if (!greater_is_operator && at_closing_angle() &&
            !at(preprocessing_op_or_punc::GREATER_EQUAL))
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

/*
 * The operators of [expr.mul], [expr.add] (on arithmetic operands),
 * [expr.shift], [expr.bit.and], [expr.xor] and [expr.or]: the operands are
 * prvalues; the result is a prvalue of their common type.
 */
analysis_result semantic_analysis::arithmetic(SyntaxRef n,
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
                   is_pointer(x.type) || class_operand(x);
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
    m.evaluation = fold_binary(b, tokens.files.floating_point_pragma, op, t,
                               fundamental_of(right.type), left.value,
                               right.value, m.value);
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
analysis_result semantic_analysis::composite_pointer_type(SyntaxRef at,
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
    } else if (null(a) && (is_pointer(b.type) || is_member_pointer(b.type))) {
        composite = b.type;
    } else if (null(b) && (is_pointer(a.type) || is_member_pointer(a.type))) {
        composite = a.type;
    } else if (is_member_pointer(a.type) && is_member_pointer(b.type)) {
        /* "if T1 is "pointer to member of C1 of type cv1 U" and T2 is
           "pointer to member of C2 of type cv2 U", for some non-function
           type U, where C1 is reference-related to C2 or C2 is
           reference-related to C1, the cv-combined type of T2 and T1 or
           the cv-combined type of T1 and T2, respectively": the same U
           here, of one level: the derived class's type, U with the union
           of cv1 and cv2 */
        const struct type x = type_table[a.type.unqualified()];
        const struct type y = type_table[b.type.unqualified()];
        type_kind u = type_table[x.operand.unqualified()].kind;
        if (a.type.unqualified() == b.type.unqualified())
            composite = a.type;
        else if (x.operand.unqualified() != y.operand.unqualified() ||
                 u == type_kind::function || u == type_kind::array)
            return not_supported(at, "expr.type");
        else if (base_subobjects(x.entity, y.entity) || x.entity == y.entity)
            composite = a.type;
        else if (base_subobjects(y.entity, x.entity))
            composite = b.type;
        else
            return not_supported(at, "expr.type");
        if (a.type.unqualified() != b.type.unqualified()) {
            const struct type c = type_table[composite.unqualified()];
            composite = qualified_type::make(
                type_table.pointer_to_member(
                    c.entity,
                    qualified_type::make(x.operand.unqualified(),
                                         x.operand.cv() | y.operand.cv())),
                cv_none);
        }
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
        } else if (type_table[x.unqualified()].kind == type_kind::class_type &&
                   type_table[y.unqualified()].kind == type_kind::class_type) {
            /* "if T1 is "pointer to cv1 C1" and T2 is "pointer to cv2 C2",
               where C1 is reference-related to C2 or C2 is
               reference-related to C1, the cv-combined type of T2 and T1 or
               the cv-combined type of T1 and T2, respectively": the base
               class's */
            entity_index cx = type_table[x.unqualified()].entity;
            entity_index cy = type_table[y.unqualified()].entity;
            bool ambiguous = false;
            qualified_type base = x;
            if (base_step(cx, cy, ambiguous))
                base = y;
            else if (!base_step(cy, cx, ambiguous))
                return not_supported(at, "expr.type");
            if (ambiguous)
                return not_supported(at, "expr.type");
            qualified_type c =
                qualified_type::make(base.unqualified(), x.cv() | y.cv());
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

/* whether operand m has class type */
bool semantic_analysis::class_operand(const expression_meaning &m) const
{
    return type_table[m.type.unqualified()].kind == type_kind::class_type;
}

/*
 * [expr.rel] and [expr.eq]: the operands are prvalues; the result is a
 * bool.
 */
analysis_result semantic_analysis::comparison(SyntaxRef n,
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
        m.evaluation =
            fold_binary(b, false, op, t, t, left.value, right.value, m.value);
        return analysis_result::well_formed;
    }
    bool pointers = is_pointer(left.type) || is_pointer(right.type) ||
                    (equality && (is_member_pointer(left.type) ||
                                  is_member_pointer(right.type)));
    bool nullptrs =
        fundamental_of(left.type) == fundamental_type::std_nullptr_t ||
        fundamental_of(right.type) == fundamental_type::std_nullptr_t;
    if (!pointers && !nullptrs) {
        /* (an operand of class type: no operator function was viable, and
           no conversion function makes a built-in one so) */
        if (!class_operand(left) && !class_operand(right) &&
            (fundamental_of(left.type) == fundamental_type::none ||
             fundamental_of(right.type) == fundamental_type::none))
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
    VTRY(r, composite_pointer_type(n, left, right));
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

/*
 * [expr.spaceship]/3-7: p <=> q of arithmetic, enumeration or object
 * pointer operands, a prvalue of std::strong_ordering or
 * std::partial_ordering (/9: <compare> shall have been included).  Its
 * value is not modelled; operand types the rules judge by a narrowing
 * conversion or by a composite type that is not an object pointer type
 * are refused.
 */
analysis_result
semantic_analysis::three_way_comparison(SyntaxRef n, expression_meaning &left,
                                        expression_meaning &right,
                                        expression_meaning &m)
{
    const char *name = "strong_ordering";
    if (is_arithmetic(left.type) && is_arithmetic(right.type)) {
        bool left_bool = fundamental_of(left.type) == fundamental_type::bool_;
        bool right_bool = fundamental_of(right.type) == fundamental_type::bool_;
        if (left_bool != right_bool)
            return ill_formed(n,
                              "if one of the operands is of type bool and "
                              "the other is not, the program is ill-formed",
                              "expr.spaceship");
        fundamental_type t = usual_arithmetic_conversions(left, right);
        bool floating = is_floating(left.type) || is_floating(right.type);
        if (floating)
            name = "partial_ordering";
        /* (a conversion between integral types may be narrowing) */
        else if (fundamental_of(left.type) != fundamental_of(right.type) ||
                 fundamental_of(left.type) != t)
            return not_supported(n, "expr.spaceship");
    } else if (is_pointer(left.type) && is_pointer(right.type)) {
        VTRY(r, composite_pointer_type(n, left, right));
        /* ([basic.compound]/3: an object pointer type includes cv void *) */
        qualified_type x = type_table[left.type.unqualified()].operand;
        qualified_type y = type_table[right.type.unqualified()].operand;
        if (type_table[x.unqualified()].kind == type_kind::function ||
            type_table[y.unqualified()].kind == type_kind::function)
            return not_supported(n, "expr.spaceship");
    } else if ((!class_operand(left) && !class_operand(right)) &&
               (fundamental_of(left.type) == fundamental_type::none ||
                fundamental_of(right.type) == fundamental_type::none ||
                is_pointer(left.type) || is_pointer(right.type))) {
        return not_supported(n, "expr.spaceship");
    } else {
        return ill_formed(n,
                          "the operands shall have arithmetic, enumeration "
                          "or pointer type",
                          "expr.spaceship");
    }
    entity_index std_ =
        entity_table.find(0, tokens.pp.identifier("std", 3) + 1);
    entity_index category = 0;
    if (std_ && is_namespace(std_) && std_namespace(std_)) {
        lookup_result found = lookup_in_namespace(
            std_,
            tokens.pp.identifier(name, static_cast<uint32_t>(strlen(name))) + 1,
            lookup_filter::any);
        if (found.ambiguous)
            return not_supported(n, "namespace.qual");
        category = found.entity;
    }
    require_complete(category);
    if (!category || entity_table[category].kind != entity_kind::class_type ||
        !entity_table[category].size)
        return ill_formed(n,
                          "the header <compare> shall be included before a "
                          "use of the built-in three-way comparison",
                          "expr.spaceship");
    m = expression_meaning();
    m.type = qualified_type::make(entity_table[category].type.unqualified(),
                                  cv_none);
    m.category = value_category::prvalue;
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
analysis_result semantic_analysis::binary(SyntaxRef n, expression_meaning &m)
{
    if (n.size() != 3 || !n[1].is_token())
        return not_supported(n, "expr.compound");
    const token &t = tokens[n[1].token()];
    if (t.kind != token_kind::operator_or_punctuator)
        return not_supported(n, "expr.compound");
    preprocessing_op_or_punc op =
        preprocessing_op_or_punc_primary(t.op_or_punc);
    token_index at = n[1].token();
    expression_meaning left, right;
    VTRY(r, expression(n[0], left));
    if (type_table[left.type.unqualified()].kind == type_kind::class_type &&
        overload_set_operand(n[2], right)) {
        /* [over.over]/1: the name of an overload set as the right operand
           of an operator function (a manipulator: os << endl) is resolved
           for the parameter of the function chosen */
        bool handled = false;
        VTRY_SET(r, binary_operator_function(n, at, op, n[0], left, n[2], right,
                                             handled, m));
        if (!handled)
            return not_supported(n, "over.over");
        return r;
    }
    VTRY_SET(r, expression(n[2], right));
    return binary_of(n, at, op, n[0], left, n[2], right, m);
}

analysis_result semantic_analysis::binary_of(SyntaxRef n, token_index at,
                                             preprocessing_op_or_punc op,
                                             SyntaxElement left_syntax,
                                             expression_meaning &left,
                                             SyntaxElement right_syntax,
                                             expression_meaning &right,
                                             expression_meaning &m)
{
    /* [over.match.oper]/2: an operand of class or enumeration type (.*
       is not overloadable) */
    bool handled = false;
    if (op != preprocessing_op_or_punc::DOT_STAR) {
        analysis_result r = binary_operator_function(
            n, at, op, left_syntax, left, right_syntax, right, handled, m);
        if (r != analysis_result::well_formed || handled)
            return r;
    }
    return builtin_binary(n, at, op, left, right, m);
}

/* the built-in operator of E1 op E2, its operands analysed */
analysis_result semantic_analysis::builtin_binary(SyntaxRef n, token_index at,
                                                  preprocessing_op_or_punc op,
                                                  expression_meaning &left,
                                                  expression_meaning &right,
                                                  expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    if (op == preprocessing_op_or_punc::DOT_STAR ||
        op == preprocessing_op_or_punc::ARROW_STAR)
        /* the built-in pointer-to-member operators
           (pointer-to-member-operator.cpp) */
        return member_pointer_operator(
            n, at, op == preprocessing_op_or_punc::ARROW_STAR, left, right, m);
    if (op == preprocessing_op_or_punc::AMPERSAND_AMPERSAND ||
        op == preprocessing_op_or_punc::BAR_BAR) {
        /* [expr.log.and]/1: "The operands are both contextually converted
           to bool.  The result is true if both operands are true and false
           otherwise. ... the second operand is not evaluated if the first
           operand is false."  [expr.log.or]/1 likewise: "the second operand
           is not evaluated if the first operand evaluates to true." */
        VTRY_SET(r, contextual_bool(n, left));
        VTRY_SET(r, contextual_bool(n, right));
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
        VTRY_SET(r, operand_value(n, right));
    /* [ext:gnu_vector_size] an operand that is a vector: lane by lane */
    if (r == analysis_result::well_formed &&
        (type_table[left.type.unqualified()].kind == type_kind::vector ||
         type_table[right.type.unqualified()].kind == type_kind::vector))
        return vector_binary(n, at, op, left, right, m);
    /* [expr.arith.conv]/1, [expr.add]/1, [expr.shift]/1: unscoped
       enumeration operands are promoted; two of one scoped enumeration
       type are compared ([expr.rel]/3, [expr.eq]/7) */
    enumeration_operands(left, right,
                         op == preprocessing_op_or_punc::LESS ||
                             op == preprocessing_op_or_punc::GREATER ||
                             op == preprocessing_op_or_punc::LESS_EQUAL ||
                             op == preprocessing_op_or_punc::GREATER_EQUAL ||
                             op == preprocessing_op_or_punc::EQUAL_EQUAL ||
                             op ==
                                 preprocessing_op_or_punc::EXCLAMATION_EQUAL ||
                             op == preprocessing_op_or_punc::SPACESHIP);
    switch (op) {
    case preprocessing_op_or_punc::SPACESHIP:
        r = three_way_comparison(n, left, right, m);
        return binary_built(r, at, op, left, right, m);
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
analysis_result semantic_analysis::conditional(SyntaxRef n,
                                               expression_meaning &m)
{
    if (n.size() == 4)
        return gnu_conditional_omitted(n, m);
    if (n.size() != 5)
        return not_supported(n, "expr.cond");
    expression_meaning c, e2, e3;
    analysis_result r = expression(n[0], c);
    VTRY_SET(r, contextual_bool(n, c));
    VTRY_SET(r, expression(n[2], e2));
    VTRY_SET(r, expression(n[4], e3));
    return conditional_operands(n, c, e2, e3, m);
}

/*
 * [expr.cond]/2: "If either the second or the third operand has type void,
 * one of the following shall hold: (2.1) The second or the third operand
 * (but not both) is a throw-expression; the result is of the type and value
 * category of the other.  (2.2) Both the second and the third operands have
 * type void; the result is of type void and is a prvalue."  Called with at
 * least one void operand.
 */
analysis_result semantic_analysis::conditional_void_operand(
    SyntaxRef n, const expression_meaning &c, const expression_meaning &e2,
    const expression_meaning &e3, expression_meaning &m)
{
    bool void2 = fundamental_of(e2.type) == fundamental_type::void_;
    bool void3 = fundamental_of(e3.type) == fundamental_type::void_;
    bool throw2 = semantics[e2.node].kind == semantic_kind::throw_exception;
    bool throw3 = semantics[e3.node].kind == semantic_kind::throw_exception;
    if (throw2 == throw3 && !(void2 && void3))
        return ill_formed(n,
                          "one operand has type void, so the other is a "
                          "throw-expression or also has type void",
                          "expr.cond");
    /* the operand that is not the throw-expression (the second when both
       are, or neither is) */
    const expression_meaning *kept_operand = &e2;
    if (throw2 && !throw3)
        kept_operand = &e3;
    const expression_meaning &kept = *kept_operand;
    bool thrown_chosen = false;
    if (c.evaluation == constness::constant) {
        thrown_chosen = throw3;
        if (c.value)
            thrown_chosen = throw2;
    }
    m = expression_meaning();
    m.type = kept.type;
    m.category = kept.category;
    m.bit_field_width = kept.bit_field_width;
    m.beyond_integral_constant = c.beyond_integral_constant ||
                                 e2.beyond_integral_constant ||
                                 e3.beyond_integral_constant;
    /* a throw-expression is never constant: only the other operand chosen
       by a constant condition keeps the result's constness */
    if (void2 && void3) {
        m.evaluation = constness::not_constant;
    } else if (c.evaluation == constness::constant && !thrown_chosen) {
        m.evaluation = kept.evaluation;
        m.entity = kept.entity;
        m.value = kept.value;
    } else if (thrown_chosen) {
        m.evaluation = constness::not_constant;
    } else {
        m.evaluation = constness::unknown;
    }
    m.node = expression_node(semantic_kind::conditional, m, n.first_token(),
                             c.node, e2.node);
    semantics[m.node].c = e3.node;
    return analysis_result::well_formed;
}

/*
 * [expr.cond]/4: "an attempt is made to form an implicit conversion sequence
 * from each of these operands to the type of the other": whether the operand
 * from converts to match to, and its conversion.  "(4.1) If E2 is an lvalue,
 * the target type is 'lvalue reference to T2', subject to the constraint that
 * in the conversion the reference must bind directly to an lvalue": a class
 * lvalue converts to a base class lvalue.  "(4.3) if E2 is a prvalue or if
 * neither of the conversion sequences above can be formed and at least one of
 * the operands has (possibly cv-qualified) class type: (4.3.1) if T1 and T2
 * are the same class type ... (4.3.2) otherwise, if T2 is a base class of T1,
 * the target type is cv1 T2 ... (4.3.3) otherwise, the target type is the
 * type that E2 would have after applying the lvalue-to-rvalue,
 * array-to-pointer, and function-to-pointer standard conversions."  "If E1
 * and E2 have class type, and the underlying class types are the same or one
 * is a base class of the other: E1 can be converted to match E2 if the class
 * of T2 is the same type as, or a base class of, the class of T1, and the
 * cv-qualification of T2 is the same cv-qualification as, or a greater
 * cv-qualification than, the cv-qualification of T1."  Applied (apply) it
 * makes the converted operand; else only whether it can.
 */
analysis_result semantic_analysis::conditional_conversion(
    SyntaxRef n, const expression_meaning &from, const expression_meaning &to,
    bool apply, bool &possible, expression_meaning &converted)
{
    possible = false;
    /* (copies: analysis below can move the type table) */
    const struct type source = type_table[from.type.unqualified()];
    const struct type target = type_table[to.type.unqualified()];
    bool source_class = source.kind == type_kind::class_type;
    bool target_class = target.kind == type_kind::class_type;
    bool ambiguous = false;
    bool to_is_base = source_class && target_class &&
                      base_step(source.entity, target.entity, ambiguous) != 0;
    bool from_is_base = source_class && target_class &&
                        base_step(target.entity, source.entity, ambiguous) != 0;
    bool cv_included = (from.type.cv() & ~to.type.cv()) == 0;
    if (ambiguous)
        return not_supported(n, "class.member.lookup");
    if (to.category == value_category::xvalue &&
        (to_is_base ||
         (source_class && has_conversion_functions(source.entity))))
        /* /4.2's rvalue reference bound directly */
        return not_supported(n, "expr.cond");
    if (to.category == value_category::lvalue && source_class && !to_is_base &&
        converts_to_lvalue_reference(source.entity))
        /* /4.1 through a conversion function returning an lvalue reference */
        return not_supported(n, "over.match.ref");
    if (to_is_base && cv_included && to.category == value_category::lvalue &&
        from.category == value_category::lvalue) {
        possible = true;
        if (!apply)
            return analysis_result::well_formed;
        converted = from;
        VTRY(r, to_base(n.first_token(), target.entity, converted));
        converted.type =
            qualified_type::make(converted.type.unqualified(), to.type.cv());
        return analysis_result::well_formed;
    }
    qualified_type wanted = to.type;
    if (to_is_base || from_is_base) {
        /* related classes: only the derived class converts, to its base
           class, with at least its cv-qualification */
        if (!to_is_base || !cv_included)
            return analysis_result::well_formed;
    } else if (!target_class) {
        /* [impl.clang] clang forms no conversion sequence to an array or
           function type: it does not apply /4.3.3's array-to-pointer and
           function-to-pointer conversions to the target (conditional-expr.cpp:
           String and "" convert only one way) */
        if (target.kind == type_kind::array ||
            target.kind == type_kind::function)
            return analysis_result::well_formed;
        expression_meaning decayed = to;
        VTRY(r, operand_value(n, decayed));
        wanted = decayed.type;
    }
    /* what no constructor or conversion function of a class can do */
    if (!to_is_base && target_class &&
        !entity_table[target.entity].first_constructor &&
        !(source_class && has_conversion_functions(source.entity)))
        return analysis_result::well_formed;
    if (!to_is_base && source_class && !target_class &&
        !has_conversion_functions(source.entity))
        return analysis_result::well_formed;
    expression_meaning operand = from;
    initialized_value made;
    if (!apply) {
        bool ok = false;
        VTRY(r, try_initialization(n, operand, wanted, false, made, ok));
        possible = ok;
        return analysis_result::well_formed;
    }
    VTRY(r, initialize_with(n, operand, wanted, false, false, made));
    possible = true;
    converted = operand;
    converted.node = made.node;
    converted.category = value_category::prvalue;
    if (target_class) {
        converted.type = wanted;
        converted.evaluation = constness::not_constant;
        converted.value = 0;
        converted.entity = 0;
        converted.zero_literal = false;
        converted.bit_field_width = 0;
    }
    return analysis_result::well_formed;
}

/*
 * [expr.cond]/4 for operands of different types, one a class: "If exactly one
 * conversion sequence can be formed, that conversion is applied to the chosen
 * operand and the converted operand is used in place of the original operand
 * for the remainder of this subclause.  Otherwise, if both sequences can be
 * formed, or one can be formed but it is the ambiguous conversion sequence,
 * the program is ill-formed."  If neither can, /6's built-in candidates
 * ([over.match.oper]) are for operands with conversion functions only.
 */
analysis_result semantic_analysis::conditional_class_operands(
    SyntaxRef n, expression_meaning &e2, expression_meaning &e3)
{
    bool second = false, third = false;
    expression_meaning converted2, converted3;
    VTRY(r, conditional_conversion(n, e2, e3, false, second, converted2));
    VTRY_SET(r, conditional_conversion(n, e3, e2, false, third, converted3));
    if (second && third)
        return ill_formed(n, "each operand converts to the type of the other",
                          "expr.cond");
    if (!second && !third) {
        bool conversions = false;
        const struct type t2 = type_table[e2.type.unqualified()];
        const struct type t3 = type_table[e3.type.unqualified()];
        if (t2.kind == type_kind::class_type)
            conversions = has_conversion_functions(t2.entity);
        if (t3.kind == type_kind::class_type)
            conversions = conversions || has_conversion_functions(t3.entity);
        if (conversions)
            return not_supported(n, "over.match.oper");
        return ill_formed(n,
                          "the operands cannot be converted to a common type",
                          "expr.cond");
    }
    if (second) {
        VTRY_SET(r,
                 conditional_conversion(n, e2, e3, true, second, converted2));
        e2 = converted2;
        return analysis_result::well_formed;
    }
    VTRY_SET(r, conditional_conversion(n, e3, e2, true, third, converted3));
    e3 = converted3;
    return analysis_result::well_formed;
}

/* [expr.cond]/2-7: the conditional expression of condition c (converted
   to bool) and operands e2 and e3, analysed */
analysis_result semantic_analysis::conditional_operands(
    SyntaxRef n, const expression_meaning &c, expression_meaning e2,
    expression_meaning e3, expression_meaning &m)
{
    analysis_result r = analysis_result::well_formed;
    if (fundamental_of(e2.type) == fundamental_type::void_ ||
        fundamental_of(e3.type) == fundamental_type::void_)
        return conditional_void_operand(n, c, e2, e3, m);
    if (e2.type.unqualified() != e3.type.unqualified() &&
        (is_class_type(e2.type) || is_class_type(e3.type)))
        VTRY_SET(r, conditional_class_operands(n, e2, e3));
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
        /* a bit-field converted to the other's more cv-qualified type does
           not bind directly ([dcl.init.ref]/5.2's temporary) */
        bool bit_field_converted =
            (cv2 != cv3 && (cv2 & ~cv3) == 0 && e2.bit_field_width) ||
            (cv2 != cv3 && (cv3 & ~cv2) == 0 && e3.bit_field_width);
        if (!bit_field_converted && (!(cv2 & ~cv3) || !(cv3 & ~cv2))) {
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
                                     n.first_token(), c.node, e2.node);
            semantics[m.node].c = e3.node;
            return analysis_result::well_formed;
        }
    }
    if (is_class_type(e2.type) &&
        e2.type.unqualified() == e3.type.unqualified()) {
        /* [expr.cond]/6: "the result is a prvalue", whose object is
           initialized from the selected operand: a glvalue operand is
           copied (a move from an xvalue) */
        uint32_t cv2 = e2.type.cv(), cv3 = e3.type.cv();
        if ((cv2 & ~cv3) && (cv3 & ~cv2))
            return ill_formed(n,
                              "the class operands' cv-qualifications are not "
                              "ordered",
                              "expr.cond");
        qualified_type common =
            qualified_type::make(e2.type.unqualified(), cv2 | cv3);
        expression_meaning *operands[2] = {&e2, &e3};
        for (expression_meaning *operand : operands) {
            if (operand->category == value_category::prvalue)
                continue;
            initialized_value copy;
            VTRY_SET(r,
                     initialize_with(n, *operand, common, false, false, copy));
            operand->node = copy.node;
            operand->type = common;
            operand->category = value_category::prvalue;
            operand->evaluation = constness::not_constant;
            operand->value = 0;
            operand->entity = 0;
            operand->bit_field_width = 0;
        }
    }
    /* [expr.cond]/6: "Otherwise, the result is a prvalue."  /7:
       "Lvalue-to-rvalue, array-to-pointer, and function-to-pointer standard
       conversions are performed on the second and third operands." */
    r = operand_value(n, e2);
    if (r == analysis_result::well_formed)
        VTRY_SET(r, operand_value(n, e3));
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
               is_member_pointer(e2.type) || is_member_pointer(e3.type) ||
               fundamental_of(e2.type) == fundamental_type::std_nullptr_t ||
               fundamental_of(e3.type) == fundamental_type::std_nullptr_t) {
        /* "pointer conversions, function pointer conversions, and
           qualification conversions are performed to bring them to their
           composite pointer type"; "Both the second and third operands have
           type std::nullptr_t or one has that type and the other is a null
           pointer constant." */
        auto pointer_like = [&](const expression_meaning &x, bool zero) {
            return is_pointer(x.type) || is_member_pointer(x.type) || zero ||
                   fundamental_of(x.type) == fundamental_type::std_nullptr_t;
        };
        if (!pointer_like(e2, zero2) || !pointer_like(e3, zero3)) {
            if (is_arithmetic(e2.type) || is_arithmetic(e3.type))
                return ill_formed(n,
                                  "a pointer and an operand that is not a "
                                  "null pointer constant have no composite "
                                  "pointer type",
                                  "expr.type");
            return not_supported(n, "expr.cond");
        }
        VTRY_SET(r, composite_pointer_type(n, e2, e3));
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
    if (is_class_type(e2.type))
        /* [expr.cond]/4.3: the operand of the less cv-qualified class type
           is converted to the other's type */
        m.type = qualified_type::make(e2.type.unqualified(),
                                      e2.type.cv() | e3.type.cv());
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
    m.node = expression_node(semantic_kind::conditional, m, n.first_token(),
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
analysis_result semantic_analysis::comma(SyntaxRef n, expression_meaning &m)
{
    constness so_far = constness::constant;
    bool any = false;
    for (uint32_t i = 0; i < n.size(); i++) {
        if (n[i].is(preprocessing_op_or_punc::COMMA))
            continue;
        expression_meaning e;
        VTRY(r, expression(n[i], e));
        if (any) {
            /* [over.match.oper]/2: an operand of class or enumeration type;
               the left operand is the comma expression before ((a, b), c) */
            SyntaxElement left = {};
            if (i == 2)
                left = n[0];
            bool handled = false;
            expression_meaning result;
            VTRY_SET(
                r, binary_operator_function(n, n[i - 1].token(),
                                            preprocessing_op_or_punc::COMMA,
                                            left, m, n[i], e, handled, result));
            if (handled) {
                so_far = combined_constness(so_far, m.evaluation);
                m = result;
                continue;
            }
        }
        if (any && destroyed_temporary(m))
            /* the discarded left operand's temporary */
            return not_supported(n, "class.temporary");
        uint32_t previous = m.node;
        if (any)
            so_far = combined_constness(so_far, m.evaluation);
        m = e;
        /* the left operand is evaluated first ([expr.comma]/1) */
        if (any)
            m.node = expression_node(semantic_kind::comma, e, first_token(n[i]),
                                     previous, e.node);
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
    if (m.node && n.size() > 1)
        semantics[m.node].evaluation = m.evaluation;
    return analysis_result::well_formed;
}

} // namespace cplusplus
