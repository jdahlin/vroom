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
 * [expr.cast]/4: "The conversions performed by a const_cast, a
 * static_cast, a static_cast followed by a const_cast, a reinterpret_cast,
 * or a reinterpret_cast followed by a const_cast, can be performed using
 * the cast notation of explicit type conversion."  Analysis has the
 * static_casts of the fundamental types and pointers that are implicit
 * conversions ([expr.static.cast]/4: "An expression e can be explicitly
 * converted to a type T if there is an implicit conversion sequence from e
 * to T"), the conversions between arithmetic types ([expr.static.cast]/10
 * for the ones that are not implicit) and to cv void
 * ([expr.static.cast]/6); the rest is unsupported.
 */
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
    /* the implicit conversions; anything else (a reinterpret_cast, a
       const_cast, an integer to a pointer) is not analysed yet */
    expression_meaning converted = m;
    r = implicit_conversion(at, converted, to, true);
    if (r == analysis_result::well_formed) {
        m = converted;
        return r;
    }
    return not_supported(at, "expr.cast");
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
