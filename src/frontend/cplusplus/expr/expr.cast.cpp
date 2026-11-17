/*
 * expr.cast.cpp: [expr.cast] Explicit type conversion (cast notation).
 *
 *     cast-expression:
 *         unary-expression
 *         ( type-id ) cast-expression
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/*
 * [expr.cast]/1: "The result of the expression (T) cast-expression is of
 * type T."  Whether a '(' begins the second alternative or a
 * unary-expression (a parenthesized expression, [expr.prim.paren]) is
 * [dcl.ambig.res]/2's rule, dcl.ambig.res.cpp.  The first alternative
 * passes its unary-expression on.
 */
node_index parser::cast_expression()
{
    /* as for assignment-expression: here for the casts and unary
       operators, which nest without one */
    nesting_level level(*this);
    if (!nesting_within_limit("an expression nested no deeper than this "
                              "implementation's limit"))
        return 0;
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return unary_expression();
    size_t mark = begin_node();
    token_index first = position;
    if (!parenthesized_type_id(true)) {
        if (failed())
            return 0;
        return unary_expression();
    }
    cast_expression();
    if (failed())
        return 0;
    return finish_node(syntax_kind::cast_expression, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [expr.cast]/4: "The conversions performed by
 *
 *   - a const_cast ([expr.const.cast]),
 *   - a static_cast ([expr.static.cast]),
 *   - a static_cast followed by a const_cast,
 *   - a reinterpret_cast ([expr.reinterpret.cast]), or
 *   - a reinterpret_cast followed by a const_cast,
 *
 * can be performed using the cast notation of explicit type conversion."
 * "If a conversion can be interpreted in more than one of the ways listed
 * above, the interpretation that appears first in the list is used".
 * Analysis has them for the fundamental types and pointers to them:
 *
 *   - to cv void, [expr.static.cast]/6;
 *   - between arithmetic types, [expr.static.cast]/4 (an implicit
 *     conversion) and /10;
 *   - the other implicit conversions ([expr.static.cast]/4: "An expression
 *     e can be explicitly converted to a type T if there is an implicit
 *     conversion sequence from e to T"): a null pointer constant to a
 *     pointer, a pointer to bool, a pointer to a pointer by [conv.ptr]/2
 *     or [conv.qual];
 *   - between pointers otherwise, pointer_conversion;
 *   - a pointer to an integral type, [expr.reinterpret.cast]/4: "A pointer
 *     can be explicitly converted to any integral type large enough to
 *     hold all values of its type";
 *   - an integral type to a pointer, [expr.reinterpret.cast]/5: "A value
 *     of integral type or enumeration type can be explicitly converted to
 *     a pointer."
 *
 * No cast converts between a pointer and a floating type: ill-formed.
 * The address is the same in each of them, the integers' mapping being
 * the implementation's ([expr.reinterpret.cast]/4 "The mapping function is
 * implementation-defined"): the target's pointer's bits.
 */
static const char cast_rule[] =
    "the cast notation performs the conversions of a const_cast, a "
    "static_cast, or a reinterpret_cast, and those followed by a "
    "const_cast";

analysis_result semantic_analysis::explicit_conversion(node_index at,
                                                       qualified_type to,
                                                       expression_meaning &m)
{
    const struct type target = type_table[to.unqualified()];
    if (target.kind == type_kind::fundamental &&
        target.fundamental == fundamental_type::void_) {
        /* [expr.static.cast]/6: "Any expression can be explicitly converted
           to type cv void, in which case it becomes a discarded-value
           expression." */
        constness evaluation = m.evaluation;
        bool beyond = m.beyond_integral_constant;
        uint32_t operand = m.node;
        m = expression_meaning();
        m.type = qualified_type::make(to.unqualified(), cv_none);
        m.evaluation = evaluation;
        m.beyond_integral_constant = beyond;
        m.node = operand;
        converted(m, conversion_kind::to_void);
        return analysis_result::well_formed;
    }
    if (target.kind != type_kind::fundamental &&
        target.kind != type_kind::pointer)
        return not_supported(at, "expr.cast");
    analysis_result r = operand_value(at, m);
    if (r != analysis_result::well_formed)
        return r;
    if (is_arithmetic(to) && is_arithmetic(m.type)) {
        convert_arithmetic(m, target.fundamental);
        return analysis_result::well_formed;
    }
    fundamental_type from = fundamental_of(m.type);
    qualified_type result = qualified_type::make(to.unqualified(), cv_none);
    if (target.kind == type_kind::pointer) {
        if (m.zero_literal || from == fundamental_type::std_nullptr_t)
            return implicit_conversion(at, m, to, true);
        if (is_pointer(m.type))
            return pointer_conversion(at, to, m);
        if (is_integral(m.type)) {
            /* [expr.const]/5: "a reinterpret_cast" is not a core constant
               expression */
            m.type = result;
            m.evaluation = constness::not_constant;
            converted(m, conversion_kind::integral_to_pointer);
            return analysis_result::well_formed;
        }
        if (from == fundamental_type::none)
            return not_supported(at, "expr.cast");
        return ill_formed(at, cast_rule, "expr.cast");
    }
    if (is_pointer(m.type)) {
        fundamental_type t = target.fundamental;
        if (t == fundamental_type::bool_)
            return implicit_conversion(at, m, to, true);
        if (!fundamental_type_is_integral(t))
            return ill_formed(at, cast_rule, "expr.cast");
        /* (the targets' pointers have 64 bits) */
        if (fundamental_type_width(tokens.files.behavior, t) < 64)
            return ill_formed(at,
                              "a pointer can be explicitly converted to any "
                              "integral type large enough to hold all values "
                              "of its type",
                              "expr.reinterpret.cast");
        m.type = result;
        m.evaluation = constness::not_constant;
        converted(m, conversion_kind::pointer_to_integral);
        return analysis_result::well_formed;
    }
    /* std::nullptr_t, the operands of other types: the implicit
       conversions, the rest not analysed yet */
    if (from == fundamental_type::none)
        return not_supported(at, "expr.cast");
    if (target.fundamental == fundamental_type::std_nullptr_t)
        return implicit_conversion(at, m, to, true);
    return not_supported(at, "expr.cast");
}

/*
 * A pointer to a pointer by a cast ([expr.cast]/4's list, in order):
 *
 *   - the static_casts that are implicit conversions: the same type,
 *     [conv.qual], [conv.ptr]/2's "pointer to cv void";
 *   - a const_cast, [expr.const.cast]/3: "For two similar types T1 and T2
 *     ([conv.qual]), a prvalue of type T1 may be explicitly converted to
 *     the type T2 using a const_cast";
 *   - [expr.static.cast]/13: "A prvalue of type "pointer to cv1 void" can
 *     be converted to a prvalue of type "pointer to cv2 T", where T is an
 *     object type", then a const_cast if cv2 is less;
 *   - a reinterpret_cast, then a const_cast: [expr.reinterpret.cast]/7
 *     "An object pointer can be explicitly converted to an object pointer
 *     of a different type", /6 "A function pointer can be explicitly
 *     converted to a function pointer of a different type", /8
 *     "Converting a function pointer to an object pointer type or vice
 *     versa is conditionally-supported" ([impl.clang]: supported, the
 *     same address).
 *
 * A pointer to a class is not analysed yet: a static_cast between a base
 * and a derived class ([expr.static.cast]/11) can change the address.
 * The last two are not core constant expressions ([expr.const]/5: "a
 * conversion from type cv void* to a pointer-to-object type", "a
 * reinterpret_cast").
 */
analysis_result semantic_analysis::pointer_conversion(node_index at,
                                                      qualified_type to,
                                                      expression_meaning &m)
{
    qualified_type result = qualified_type::make(to.unqualified(), cv_none);
    qualified_type pointee = type_table[m.type.unqualified()].operand;
    qualified_type target_pointee = type_table[to.unqualified()].operand;
    const struct type fp = type_table[pointee.unqualified()];
    const struct type tp = type_table[target_pointee.unqualified()];
    if (fp.kind == type_kind::class_type || tp.kind == type_kind::class_type)
        return not_supported(at, "expr.static.cast");
    bool to_void = tp.kind == type_kind::fundamental &&
                   tp.fundamental == fundamental_type::void_;
    if (m.type.unqualified() == to.unqualified() ||
        qualification_convertible(m.type, to) ||
        (to_void && is_object_pointer(m.type) &&
         !(pointee.cv() & ~target_pointee.cv())))
        return implicit_conversion(at, m, to, true);
    if (similar(m.type, to)) {
        m.type = result;
        converted(m, conversion_kind::qualification_cast);
        return analysis_result::well_formed;
    }
    m.type = result;
    m.evaluation = constness::not_constant;
    converted(m, conversion_kind::pointer_cast);
    return analysis_result::well_formed;
}

/*
 * [expr.cast]/1: "The result of the expression (T) cast-expression is of
 * type T.  The result is an lvalue if T is an lvalue reference type or an
 * rvalue reference to function type and an xvalue if T is an rvalue
 * reference to object type; otherwise the result is a prvalue."
 */
analysis_result semantic_analysis::cast(node_index n, expression_meaning &m)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (node.child_count != 4 ||
        !syntax_is(tree, children[1], syntax_kind::type_id))
        return not_supported(n, "expr.cast");
    qualified_type to;
    analysis_result r = type_id(children[1].index(), to);
    if (r == analysis_result::well_formed)
        r = expression(children[3], m);
    if (r != analysis_result::well_formed)
        return r;
    /* C++03 [expr.const]/1: "Floating literals can appear only if they are
       cast to integral or enumeration types." */
    bool floating_literal_to_integral =
        children[3].is_token() &&
        tokens[children[3].index()].literal ==
            literal_kind::floating_point_literal &&
        is_integral(to);
    r = explicit_conversion(n, to, m);
    if (r == analysis_result::well_formed && floating_literal_to_integral)
        m.beyond_integral_constant = false;
    return r;
}

} // namespace cplusplus
