/*
 * expr.pre.cpp: [expr.pre] Expressions: the analysis of one expression,
 * by the nonterminal the parser gave it.
 *
 * [expr.pre]/1: "An expression is a sequence of operators and operands
 * that specifies a computation."  Each nonterminal's rules are in the file
 * of its subclause (expr.prim.cpp, expr.unary.cpp, expr.compound.cpp ...);
 * here they are dispatched, and every expression with a meaning is
 * recorded for the type and constant claims (cxxsema -type-claims).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::expression(syntax_child e,
                                              expression_meaning &m)
{
    m = expression_meaning();
    analysis_result r = expression_kind(e, m);
    if (r != analysis_result::well_formed)
        return r;
    /* [expr.type]/2: "If a prvalue initially has the type "cv T", where T
       is a cv-unqualified non-class, non-array type, the type of the
       expression is adjusted to T prior to any further analysis." */
    const struct type record = type_table[m.type.unqualified()];
    if (m.category == value_category::prvalue &&
        record.kind != type_kind::class_type && record.kind != type_kind::array)
        m.type = qualified_type::make(m.type.unqualified(), cv_none);
    analysed_expression x;
    x.at = e;
    x.declaration = current_declaration;
    x.meaning = m;
    expressions.append(x);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::expression_kind(syntax_child e,
                                                   expression_meaning &m)
{
    if (e.is_token()) {
        const token &t = tokens[e.index()];
        /* true, false and nullptr are keywords that are literals */
        if (t.literal != literal_kind::none)
            return literal(e.index(), m);
        if (t.kind == token_kind::identifier)
            return id_expression(e.index(), m);
        if (t.kind == token_kind::keyword &&
            p.keyword_of(t) == standard_keyword::this_)
            return not_supported(current_declaration, "expr.prim.this");
        return not_supported(current_declaration, "expr.prim");
    }
    node_index n = e.index();
    switch (tree[n].kind) {
    case syntax_kind::primary_expression:
        return parenthesized(n, m);
    case syntax_kind::unary_expression:
        return unary(n, m);
    case syntax_kind::postfix_expression:
        return postfix(n, m);
    case syntax_kind::cast_expression:
        return cast(n, m);
    case syntax_kind::multiplicative_expression:
    case syntax_kind::additive_expression:
    case syntax_kind::shift_expression:
    case syntax_kind::relational_expression:
    case syntax_kind::equality_expression:
    case syntax_kind::and_expression:
    case syntax_kind::exclusive_or_expression:
    case syntax_kind::inclusive_or_expression:
    case syntax_kind::logical_and_expression:
    case syntax_kind::logical_or_expression:
        return binary(n, m);
    case syntax_kind::conditional_expression:
        return conditional(n, m);
    case syntax_kind::assignment_expression:
        return assignment(n, m);
    case syntax_kind::expression:
        return comma(n, m);
    default:
        return not_supported(n);
    }
}

} // namespace cplusplus
