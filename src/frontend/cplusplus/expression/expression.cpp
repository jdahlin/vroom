/*
 * expression.cpp: [expr.pre] Expressions: the analysis of one expression,
 * by the nonterminal the parser gave it.
 *
 * [expr.pre]/1: "An expression is a sequence of operators and operands
 * that specifies a computation."  Each nonterminal's rules are in the file
 * of its subclause (primary-expression.cpp, unary-expression.cpp,
 * binary-expression.cpp ...); here they are dispatched, and every expression
 * with a meaning is recorded for the type and constant claims
 * (-fcxx-dump=type-claims).
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

analysis_result semantic_analysis::expression(SyntaxElement e,
                                              expression_meaning &m)
{
    m = expression_meaning();
    VTRY(r, expression_kind(e, m));
    /* [ext:clang_atomic], [ext:clang_bit_int] the operations on an atomic
       object or a _BitInt are not analysed yet */
    type_kind k = type_table[m.type.unqualified()].kind;
    if (k == type_kind::complex ||
        (k == type_kind::fundamental &&
         type_table[m.type.unqualified()].fundamental ==
             fundamental_type::float128)) {
        /* [ext:gnu_complex], [ext:gnu_float128] complex and binary128
           arithmetic is not analysed yet */
        if (e.is_token())
            return not_supported(current_declaration, "ext.gnu");
        return not_supported(e.index(), "ext.gnu");
    }
    if (k == type_kind::atomic || k == type_kind::bit_int ||
        k == type_kind::ext_vector) {
        if (e.is_token())
            return not_supported(current_declaration, "ext.clang");
        return not_supported(e.index(), "ext.clang");
    }
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
    /* in a lambda's body a claim is an unevaluated operand, where clang
       gives a name of an entity the lambda captures by copy the entity's
       type, not the capture's ([expr.prim.id.unqual]/2) */
    if (current_lambda())
        x.placeable = false;
    /* a name in its own declaration's initializer: [basic.scope.pdecl]/1
       "The point of declaration for a name is immediately after its
       complete declarator ([dcl.decl]) and before its initializer (if
       any)", but the initializer can complete the type (an array's bound),
       so after the declaration the name may have another type */
    if (e.is_identifier()) {
        const struct type &named = type_table[m.type.unqualified()];
        if (named.kind == type_kind::array && !named.bound_known)
            x.placeable = false;
    }
    expressions.append(x);
    return analysis_result::well_formed;
}

analysis_result semantic_analysis::expression_kind(SyntaxElement e,
                                                   expression_meaning &m)
{
    if (e.is_token()) {
        const token &t = tokens[e.token()];
        /* true, false and nullptr are keywords that are literals */
        if (t.literal == literal_kind::user_defined_literal)
            return user_defined_literal(e.token(), m);
        if (t.literal != literal_kind::none) {
            /* the value of a literal analysis does not evaluate (a long
               double's) is the token's */
            analysis_result r = literal(e.token(), m);
            semantic_kind kind = semantic_kind::constant;
            if (t.literal == literal_kind::string_literal)
                kind = semantic_kind::string_literal;
            if (r == analysis_result::well_formed)
                m.node = expression_node(kind, m, e.token());
            return r;
        }
        if (t.kind == token_kind::identifier)
            return id_expression(e.token(), m);
        if (t.kind == token_kind::keyword &&
            p.keyword_of(t) == standard_keyword::this_)
            return this_pointer(e.token(), m);
        return not_supported(current_declaration, "expr.prim");
    }
    switch (e.kind()) {
    case syntax_kind::gnu_extension_expression:
        /* [ext:gnu_extension_keyword]: __extension__ changes no meaning */
        return expression(e[1], m);
    case syntax_kind::primary_expression:
        return parenthesized(e, m);
    case syntax_kind::qualified_id:
        return qualified_id(e, m);
    case syntax_kind::unary_expression:
        return unary(e, m);
    case syntax_kind::postfix_expression:
        return postfix(e, m);
    case syntax_kind::cast_expression:
        return cast(e, m);
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
        return binary(e, m);
    case syntax_kind::conditional_expression:
        return conditional(e, m);
    case syntax_kind::assignment_expression:
        return assignment(e, m);
    case syntax_kind::expression:
        return comma(e, m);
    case syntax_kind::new_expression:
        return new_expression(e, m);
    case syntax_kind::delete_expression:
        return delete_expression(e, m);
    case syntax_kind::throw_expression:
        return throw_expression(e, m);
    case syntax_kind::fold_expression:
        return fold(e, m);
    case syntax_kind::lambda_expression:
        return lambda_expression(e, m);
    case syntax_kind::noexcept_expression:
        return noexcept_operator(e, m);
    case syntax_kind::simple_template_id:
        return variable_template_id(e, m);
    case syntax_kind::requires_expression:
        return requires_expression(e, m);
    case syntax_kind::clang_type_trait_expression:
        return clang_type_trait(e, m);
    case syntax_kind::gnu_offsetof_expression:
        return gnu_offsetof(e, m);
    case syntax_kind::clang_bit_cast_expression:
        return clang_bit_cast(e, m);
    case syntax_kind::clang_convertvector_expression:
        return clang_convertvector(e, m);
    case syntax_kind::clang_source_location_expression:
        return clang_source_location(e, m);
    default:
        return not_supported(e);
    }
}

} // namespace cplusplus
