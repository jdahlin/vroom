/*
 * temp.concept.cpp: [temp.concept] Concept definitions, and
 * [temp.deduct.guide] Deduction guides.
 *
 *     concept-definition:
 *       concept concept-name = constraint-expression ;
 *
 *     concept-name:
 *       identifier
 *
 * [temp.constr.decl]:
 *
 *     constraint-expression:
 *         logical-or-expression
 *
 * [temp.concept]/1: "A concept is a template that defines constraints on
 * its template arguments."  /2: "A concept-definition declares a concept.
 * Its identifier becomes a concept-name referring to that concept within
 * its scope."  Its point of declaration is the end of the concept-name
 * (P1787R6 aside, the definition may not refer to itself: /6, semantic).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::concept_definition()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* concept */
    if (!at_identifier())
        return expected("a concept-name", "temp.concept");
    uint32_t name = tokens.identifier(position);
    add_token();
    if (!accept(preprocessing_op_or_punc::EQUAL))
        return expected("'=' after the concept-name", "temp.concept");
    names.declare(name, name_kind::concept_name);
    binary_expression(0); /* logical-or-expression */
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the concept-definition", "temp.concept");
    return finish_node(syntax_kind::concept_definition, mark, first);
}

/*
 * [temp.deduct.guide]/1:
 *
 *     deduction-guide:
 *         explicit-specifier_opt template-name ( parameter-declaration-clause ) -> simple-template-id ;
 *
 * /3: "The template-name shall be the same identifier as the template-name
 * of the simple-template-id."  A template-name of a class template before
 * '(' and, after the parentheses, '->' is a deduction-guide: no other
 * declaration has that form (a class template's name is no
 * decl-specifier-seq without its arguments in a declaration followed by a
 * trailing-return-type, which needs a placeholder, [dcl.fct]/2).  P0091R3
 * (C++17).
 */
bool parser::deduction_guide_follows()
{
    unsigned i = 0;
    if (at(standard_keyword::explicit_)) {
        i = 1;
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1))
            i = group_end(1);
    }
    if (!at_identifier(i) || !at(preprocessing_op_or_punc::LEFT_PARENTHESIS, i + 1))
        return false;
    unsigned after = group_end(i + 1);
    if (!at(preprocessing_op_or_punc::ARROW, after))
        return false;
    return names.lookup_unqualified(tokens.identifier(position + i)) ==
           name_kind::template_name;
}

node_index parser::deduction_guide()
{
    if (!tokens.files.behavior.has(language_feature::deduction_guides))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    if (at(standard_keyword::explicit_)) {
        if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1))
            explicit_specifier();
        else
            add_token();
        if (failed())
            return 0;
    }
    add_token(); /* template-name */
    add_token(); /* ( */
    {
        declarative_region parameters(names, scope_kind::function_prototype);
        parameter_declaration_clause();
        if (failed())
            return 0;
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the parameters", "temp.deduct.guide");
    if (!accept(preprocessing_op_or_punc::ARROW))
        return expected("'->' in the deduction-guide", "temp.deduct.guide");
    if (!at_identifier() || !at(preprocessing_op_or_punc::LESS, 1))
        return expected("a simple-template-id", "temp.deduct.guide");
    size_t id_mark = begin_node();
    token_index id_first = position;
    add_token();
    template_id_rest(id_mark, id_first, syntax_kind::simple_template_id);
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the deduction-guide", "temp.deduct.guide");
    return finish_node(syntax_kind::deduction_guide, mark, first);
}

} // namespace cplusplus
