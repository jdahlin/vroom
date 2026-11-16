/*
 * expr.new.cpp: [expr.new] New.
 *
 *     new-expression:
 *         ::_opt new new-placement_opt new-type-id new-initializer_opt
 *         ::_opt new new-placement_opt ( type-id ) new-initializer_opt
 *
 *     new-placement:
 *         ( expression-list )
 *
 *     new-initializer:
 *         ( expression-list_opt )
 *         braced-init-list
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * A '(' after new begins a new-placement or the ( type-id ) of the second
 * alternative: [dcl.ambig.res]/2 makes it the type-id when it can be one.
 * After a new-placement a '(' is the second alternative's.
 */
node_index parser::new_expression()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    accept(preprocessing_op_or_punc::COLON_COLON);
    add_token(); /* new */
    bool type = false;
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        type = parenthesized_type_id(false);
        if (failed())
            return 0;
        if (!type) {
            size_t placement_mark = begin_node();
            token_index placement_first = position;
            add_token();
            if (at(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("an expression-list in the new-placement",
                                "expr.new");
            expression_list();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                return expected("')' to close the new-placement", "expr.new");
            finish_node(syntax_kind::new_placement, placement_mark,
                        placement_first);
            if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
                add_token();
                type_id();
                if (failed())
                    return 0;
                if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
                    return expected("')' after the type-id", "expr.new");
                type = true;
            }
        }
    }
    if (!type) {
        new_type_id();
        if (failed())
            return 0;
    }
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        size_t initializer_mark = begin_node();
        token_index initializer_first = position;
        if (!parenthesized_expression_list("')' to close the new-initializer", "expr.new"))
            return 0;
        finish_node(syntax_kind::new_initializer, initializer_mark,
                    initializer_first);
    } else if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
        /* new-initializer: braced-init-list passes through */
        if (list_initialization_missing())
            return 0;
        braced_init_list();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::new_expression, mark, first);
}

/*
 *     new-type-id:
 *         type-specifier-seq new-declarator_opt
 *
 * [expr.new]/3: "The new-type-id in a new-expression is the longest
 * possible sequence of new-declarators", so a ptr-operator or '[' after the
 * type-specifier-seq is always the new-declarator's.
 */
node_index parser::new_type_id()
{
    size_t mark = begin_node();
    token_index first = position;
    decl_specifier_facts specifiers;
    specifiers.type_specifier_seq = true;
    /* [temp.res]/5: "a type-id in a new-expression" */
    specifiers.implicit_typename =
        tokens.files.behavior.has(language_feature::implicit_typename);
    decl_specifier_seq(specifiers, true);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a type-specifier-seq after new", "expr.new");
    new_declarator();
    if (failed())
        return 0;
    return finish_node(syntax_kind::new_type_id, mark, first);
}

/*
 *     new-declarator:
 *         ptr-operator new-declarator_opt
 *         noptr-new-declarator
 *
 *     noptr-new-declarator:
 *         [ expression_opt ] attribute-specifier-seq_opt
 *         noptr-new-declarator [ constant-expression ] attribute-specifier-seq_opt
 *
 * new-declarator_opt: nothing (0, no failure) when none starts here.  The
 * bounds of a noptr-new-declarator are one node's children.  [[ after the
 * type is no bound ([dcl.attr.grammar]/7).
 *
 * Before C++11 (N2118) && is no ptr-operator, so new int && b is a
 * logical-and-expression there.
 */
node_index parser::new_declarator()
{
    size_t mark = begin_node();
    token_index first = position;
    if (at(preprocessing_op_or_punc::AMPERSAND_AMPERSAND) &&
        !tokens.files.behavior.has(language_feature::rvalue_references))
        return 0;
    if (at_ptr_operator()) {
        derivation kind = derivation::none;
        ptr_operator(&kind);
        if (failed())
            return 0;
        new_declarator();
        if (failed())
            return 0;
        return finish_node(syntax_kind::new_declarator, mark, first);
    }
    if (!at(preprocessing_op_or_punc::LEFT_BRACKET))
        return 0;
    bool first_bound = true;
    while (at(preprocessing_op_or_punc::LEFT_BRACKET)) {
        if (at(preprocessing_op_or_punc::LEFT_BRACKET, 1))
            return misplaced_attribute_brackets();
        add_token();
        if (first_bound) {
            if (!at(preprocessing_op_or_punc::RIGHT_BRACKET))
                expression();
        } else {
            if (at(preprocessing_op_or_punc::RIGHT_BRACKET))
                return expected("a constant-expression: only the first "
                                "bound of a new-declarator may be omitted",
                                "expr.new");
            constant_expression();
        }
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
            return expected("']' to close the array bound", "expr.new");
        attribute_specifier_seq(true);
        if (failed())
            return 0;
        first_bound = false;
    }
    return finish_node(syntax_kind::noptr_new_declarator, mark, first);
}

} // namespace cplusplus
