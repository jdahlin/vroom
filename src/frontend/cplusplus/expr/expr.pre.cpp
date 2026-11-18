/*
 * expr.pre.cpp: [expr.pre] Expressions: the analysis of one expression,
 * by the nonterminal the parser gave it.
 *
 * [expr.pre]/1: "An expression is a sequence of operators and operands
 * that specifies a computation."  Each nonterminal's rules are in the file
 * of its subclause (expr.prim.cpp, expr.unary.cpp, expr.compound.cpp ...);
 * here they are dispatched, and every expression with a meaning is
 * recorded for the type and constant claims (-fcxx-dump=type-claims).
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
    if (m.node)
        semantics[m.node].type = m.type;
    analysed_expression x;
    x.at = e;
    x.declaration = current_declaration;
    x.meaning = m;
    x.place = claim_place;
    x.placeable = current_scope == claim_scope;
    /* a name in its own declaration's initializer: [basic.scope.pdecl]/1
       "The point of declaration for a name is immediately after its
       complete declarator ([dcl.decl]) and before its initializer (if
       any)", but the initializer can complete the type (an array's bound),
       so after the declaration the name may have another type */
    if (e.is_token() && tokens[e.index()].kind == token_kind::identifier) {
        const struct type &named = type_table[m.type.unqualified()];
        if (named.kind == type_kind::array && !named.bound_known)
            x.placeable = false;
    }
    expressions.append(x);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::expression_kind(syntax_child e,
                                                   expression_meaning &m)
{
    if (e.is_token()) {
        const token &t = tokens[e.index()];
        /* true, false and nullptr are keywords that are literals */
        if (t.literal == literal_kind::user_defined_literal)
            return user_defined_literal(e.index(), m);
        if (t.literal != literal_kind::none) {
            /* the value of a literal analysis does not evaluate (a long
               double's) is the token's */
            analysis_result r = literal(e.index(), m);
            semantic_kind kind = semantic_kind::constant;
            if (t.literal == literal_kind::string_literal)
                kind = semantic_kind::string_literal;
            if (r == analysis_result::well_formed)
                m.node = expression_node(kind, m, e.index());
            return r;
        }
        if (t.kind == token_kind::identifier)
            return id_expression(e.index(), m);
        if (t.kind == token_kind::keyword &&
            p.keyword_of(t) == standard_keyword::this_)
            return this_pointer(e.index(), m);
        return not_supported(current_declaration, "expr.prim");
    }
    node_index n = e.index();
    switch (tree[n].kind) {
    case syntax_kind::primary_expression:
        return parenthesized(n, m);
    case syntax_kind::qualified_id:
        return qualified_id(n, m);
    case syntax_kind::unary_expression:
        return unary(n, m);
    case syntax_kind::postfix_expression:
        return postfix(n, m);
    case syntax_kind::cast_expression:
        return cast(n, m);
    case syntax_kind::pm_expression:
    case syntax_kind::compare_expression:
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
    case syntax_kind::new_expression:
        return new_expression(n, m);
    case syntax_kind::delete_expression:
        return delete_expression(n, m);
    default:
        return not_supported(n);
    }
}

} // namespace cplusplus
