/*
 * typeof.cpp: [ext:gnu_typeof] (GCC manual 6.7, Referring to a Type with
 * typeof): the type of an expression, or a type-id itself, as a
 * type-specifier.  glibc's <math.h> uses it in iscanonical.
 *
 *     simple-type-specifier:
 *         gnu-typeof-specifier
 *
 *     gnu-typeof-specifier:
 *         typeof ( expression )
 *         typeof ( type-id )
 *
 * typeof, __typeof and __typeof__ spell it.  The operand is unevaluated.
 * Unlike decltype, an expression's value category does not add a
 * reference: typeof(e) is the type of e ([expr.type]/1: an expression's
 * type is never a reference), and a type-id gives its type with the
 * qualifiers it has.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::gnu_typeof_specifier()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* typeof */
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after typeof", "ext.gnu");
    if (!parenthesized_type_id(false)) {
        if (failed())
            return 0;
        add_token();
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the operand of typeof", "ext.gnu");
    }
    return finish_node(syntax_kind::gnu_typeof_specifier, mark, first);
}

/* [ext:gnu_typeof] the type the specifier denotes */
analysis_result semantic_analysis::gnu_typeof_type(SyntaxRef n,
                                                   qualified_type &type)
{
    if (n.size() != 4)
        return not_supported(n, "ext.gnu");
    SyntaxElement operand = n[2];
    if (operand.is(syntax_kind::type_id))
        return type_id(operand, type, nullptr);
    /* a type-dependent operand: as decltype's */
    if (names_dependent_entity(n.first_token(), n.end_token())) {
        if (instantiating || class_instantiating || substituting)
            return not_supported(n, "temp.dep.expr");
        type = qualified_type::make(dependent_written_type(n), cv_none);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    unevaluated++;
    /* only the type of a binary128 object is used */
    analysis_result r = passed_expression(operand, m);
    unevaluated--;
    if (r != analysis_result::well_formed)
        return r;
    if (m.bit_field_width)
        return not_supported(n, "class.bit");
    const struct type record = type_table[m.type.unqualified()];
    if (record.kind == type_kind::none ||
        record.kind == type_kind::placeholder ||
        record.kind == type_kind::dependent)
        return not_supported(n, "ext.gnu");
    type = m.type;
    return analysis_result::well_formed;
}

} // namespace cplusplus
