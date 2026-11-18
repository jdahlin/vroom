/*
 * dcl.type.decltype.cpp: [dcl.type.decltype] Decltype specifiers: the type
 * a decltype-specifier denotes (/1).  The grammar is in dcl.spec.cpp.
 *
 * Not analysed: an operand naming a non-type template-parameter (/1's
 * second case, which has no rules yet), and
 * /2's prvalue of incomplete or abstract class type.
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* whether operand is an unparenthesized id-expression or class member
   access ([dcl.type.decltype]/1's third case) */
bool semantic_analysis::names_entity(syntax_child operand) const
{
    if (operand.is_token())
        return tokens[operand.index()].kind == token_kind::identifier;
    const syntax_node &node = tree[operand.index()];
    if (node.kind == syntax_kind::qualified_id)
        return true;
    if (node.kind != syntax_kind::postfix_expression || node.child_count != 3)
        return false;
    syntax_child op = tree.children(operand.index())[1];
    return syntax_is_punctuator(tokens, op, preprocessing_op_or_punc::DOT) ||
           syntax_is_punctuator(tokens, op, preprocessing_op_or_punc::ARROW);
}

/*
 * [dcl.type.decltype]/1: "otherwise, if E is an unparenthesized
 * id-expression or an unparenthesized class member access, decltype(E) is
 * the type of the entity named by E. ... otherwise, if E is an xvalue,
 * decltype(E) is T&&, where T is the type of E; otherwise, if E is an
 * lvalue, decltype(E) is T&, where T is the type of E; otherwise,
 * decltype(E) is the type of E."
 */
bool semantic_analysis::decltype_of(const expression_meaning &m, bool named,
                                    qualified_type &type)
{
    if (named) {
        if (!m.node)
            return false;
        /* "if E is an unparenthesized id-expression naming a structured
           binding ([dcl.struct.bind]), decltype(E) is the referenced type
           as given in the specification of the structured binding
           declaration" */
        if (m.binding) {
            type = entity_table[m.binding].type;
            return true;
        }
        const semantic_node &n = semantics[m.node];
        if (n.kind == semantic_kind::entity && n.a) {
            type = entity_table[n.a].type;
            return true;
        }
        if (n.kind == semantic_kind::member && n.b) {
            type = entity_table[n.b].type;
            return true;
        }
        /* an enumerator: a prvalue of its enumeration's type */
        if (n.kind == semantic_kind::constant &&
            m.category == value_category::prvalue) {
            type = m.type;
            return true;
        }
        return false;
    }
    const struct type record = type_table[m.type.unqualified()];
    if (record.kind == type_kind::none ||
        record.kind == type_kind::placeholder ||
        record.kind == type_kind::dependent)
        return false;
    type_index reference = 0;
    if (m.category == value_category::xvalue)
        reference = type_table.rvalue_reference_to(m.type);
    if (m.category == value_category::lvalue)
        reference = type_table.lvalue_reference_to(m.type);
    type = m.type;
    if (reference)
        type = qualified_type::make(reference, cv_none);
    return true;
}

/*
 * [dcl.type.decltype]:
 *
 *     decltype-specifier:
 *         decltype ( expression )
 *
 * "The operand of the decltype specifier is an unevaluated operand".
 */
analysis_result semantic_analysis::decltype_type(node_index n,
                                                 qualified_type &type)
{
    const syntax_node &node = tree[n];
    if (node.child_count != 4)
        return not_supported(n, "dcl.type.decltype");
    syntax_child operand = tree.children(n)[2];
    /* in a class's member-specification: the names of members whose
       declarations analysis has no rules for are not held */
    if (defining_class)
        return not_supported(n, "class.mem");
    /* an operand that names a template parameter (or a type that depends
       on one) is type-dependent ([temp.dep.expr]): not analysed */
    if (names_dependent_entity(tree[n].first_token, tree[n].end_token)) {
        /* [temp.dep.type]/10.3: "denoted by decltype(expression), where
           expression is type-dependent" a dependent type, in a template's
           own declaration (an instantiation's names are bound to
           arguments) */
        if (instantiating || class_instantiating || substituting)
            return not_supported(n, "temp.dep.expr");
        type = qualified_type::make(dependent_written_type(n), cv_none);
        return analysis_result::well_formed;
    }
    expression_meaning m;
    unevaluated++;
    analysis_result r = expression(operand, m);
    unevaluated--;
    if (r != analysis_result::well_formed)
        return r;
    if (m.bit_field_width && !names_entity(operand))
        return not_supported(n, "class.bit");
    bool named = names_entity(operand);
    if (!decltype_of(m, named, type))
        return not_supported(n, "dcl.type.decltype");
    return analysis_result::well_formed;
}

/* whether a name in [first, end) denotes a template parameter, or a
   variable or type whose type depends on one ([temp.dep.expr]); a
   function template's type depends on its own parameters, not the
   call's: only parameters, packs, variables and type names count */
bool semantic_analysis::names_dependent_entity(token_index first,
                                               token_index end) const
{
    for (token_index t = first; t < end; t++) {
        if (tokens[t].kind != token_kind::identifier)
            continue;
        lookup_result found =
            lookup_unqualified(tokens.identifier(t), lookup_filter::any);
        entity_index e = found.entity;
        if (!e)
            continue;
        entity_kind k = entity_table[e].kind;
        if (k == entity_kind::none)
            return true;
        if ((k == entity_kind::variable || k == entity_kind::typedef_name) &&
            depends_on_template_parameter(entity_table[e].type))
            return true;
    }
    return false;
}

/* the dependent type of decltype-specifier n: one for the operands of the
   same tokens */
type_index semantic_analysis::dependent_written_type(node_index n)
{
    const syntax_node &x = tree[n];
    for (entity_index e : dependent_written_types) {
        const syntax_node &y =
            tree[static_cast<node_index>(entity_table[e].value)];
        bool same = y.end_token - y.first_token == x.end_token - x.first_token;
        for (token_index i = 0; same && i < x.end_token - x.first_token; i++)
            same = same_token_spelling(x.first_token + i, y.first_token + i);
        if (same)
            return entity_table[e].type.unqualified();
    }
    entity_index e = entity_table.make(entity_kind::dependent_written, 0, 0);
    entity_table[e].value = n;
    entity_table[e].type = qualified_type::make(
        type_table.of_entity(type_kind::dependent, e), cv_none);
    dependent_written_types.append(e);
    return entity_table[e].type.unqualified();
}

} // namespace cplusplus
