/*
 * convertvector.cpp: [ext:clang_convertvector] (clang Language Extensions,
 * Vector Operations, __builtin_convertvector): each element of a vector
 * converted to the element type of a vector type of as many elements, as
 * a C cast of each would.  libc++'s <__algorithm/simd_utils.h> converts a
 * comparison's mask to a vector of bool with it.
 *
 *     primary-expression:
 *         clang-convertvector-expression
 *
 *     clang-convertvector-expression:
 *         __builtin_convertvector ( assignment-expression , type-id )
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_convertvector_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_convertvector */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after __builtin_convertvector", "ext.clang");
    assignment_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::COMMA))
        return expected("',' after the vector", "ext.clang");
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end __builtin_convertvector", "ext.clang");
    return finish_node(syntax_kind::clang_convertvector_expression, mark,
                       first);
}

/* [ext:clang_convertvector] a prvalue of the type-id's type; the operand
   and the type-id are vectors of as many elements.  A dependent one waits
   for the instantiation ([temp.dep.constexpr]) */
analysis_result semantic_analysis::clang_convertvector(node_index n,
                                                       expression_meaning &m)
{
    auto children = syntax(n);
    analysis_result r = expression(children[2], m);
    if (r != analysis_result::well_formed)
        return r;
    qualified_type to;
    r = type_id(children[4], to);
    if (r != analysis_result::well_formed)
        return r;
    if (depends_on_template_parameter(to) ||
        depends_on_template_parameter(m.type))
        return not_supported(n, "temp.dep.constexpr");
    to = qualified_type::make(to.unqualified(), cv_none);
    r = operand_value(n, m);
    if (r != analysis_result::well_formed)
        return r;
    const struct type &from_type = type_table[m.type.unqualified()];
    const struct type &to_type = type_table[to.unqualified()];
    if (from_type.kind != type_kind::ext_vector ||
        to_type.kind != type_kind::ext_vector ||
        from_type.bound != to_type.bound)
        return ill_formed(n,
                          "first two arguments to __builtin_convertvector "
                          "must have the same number of elements",
                          "ext.clang");
    m.evaluation = constness::not_constant;
    m.type = to;
    m.zero_literal = false;
    converted(m, conversion_kind::vector_convert);
    return analysis_result::well_formed;
}

} // namespace cplusplus
