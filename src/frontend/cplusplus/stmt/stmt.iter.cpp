/*
 * stmt.iter.cpp: [stmt.iter] Iteration statements, with [stmt.while],
 * [stmt.do], [stmt.for] and [stmt.ranged].
 *
 *     iteration-statement:
 *         while ( condition ) statement
 *         do statement while ( expression ) ;
 *         for ( init-statement condition_opt ; expression_opt ) statement
 *         for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 *     for-range-declaration:
 *         attribute-specifier-seq_opt decl-specifier-seq declarator
 *         attribute-specifier-seq_opt decl-specifier-seq ref-qualifier_opt [ identifier-list ]
 *
 *     for-range-initializer:
 *         expr-or-braced-init-list
 *
 * Which for statement it is: the range-based one has a ':' after its last
 * ';' (scan_parenthesized).  The range-based for statement is N2930's
 * (C++11), its init-statement P0614R1's (C++20); before them they are
 * [ext.clang] clang's extensions, P9's.  The structured binding of the
 * second for-range-declaration is P5's; for-range-initializer passes its
 * operand on.
 */
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::iteration_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    standard_keyword k = keyword_of(peek());
    add_token();
    if (k == standard_keyword::do_) {
        substatement();
        if (failed())
            return 0;
        if (!accept(standard_keyword::while_))
            return expected("while after the substatement of do", "stmt.do");
        if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
            return expected("'(' after while", "stmt.do");
        expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' after the expression", "stmt.do");
        if (!accept(preprocessing_op_or_punc::SEMICOLON))
            return expected("';' after the do statement", "stmt.do");
        return finish_node(syntax_kind::iteration_statement, mark, first);
    }
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'('", "stmt.iter");
    declarative_region statement_scope(names, scope_kind::block);
    if (k == standard_keyword::while_) {
        condition(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
        if (failed())
            return 0;
    } else {
        parenthesized_parts parts = scan_parenthesized(*this);
        if (parts.range_colon) {
            if (!b.has(language_feature::range_based_for))
                return beyond_this_step();
            if (parts.semicolons) {
                if (!b.has(language_feature::range_for_init_statement))
                    return beyond_this_step();
                init_statement();
                if (failed())
                    return 0;
            }
            for_range_declaration();
            if (failed())
                return 0;
            if (!accept(preprocessing_op_or_punc::COLON))
                return expected("':' after the for-range-declaration",
                                "stmt.ranged");
            if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
                if (list_initialization_missing())
                    return 0;
                braced_init_list();
            } else {
                expression();
            }
            if (failed())
                return 0;
        } else {
            init_statement();
            if (failed())
                return 0;
            if (!at(preprocessing_op_or_punc::SEMICOLON)) {
                condition(preprocessing_op_or_punc::SEMICOLON);
                if (failed())
                    return 0;
            }
            if (!accept(preprocessing_op_or_punc::SEMICOLON))
                return expected("';' after the condition", "stmt.for");
            if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
                expression();
                if (failed())
                    return 0;
            }
        }
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')'", "stmt.iter");
    substatement();
    if (failed())
        return 0;
    return finish_node(syntax_kind::iteration_statement, mark, first);
}

/* the name is declared in the for statement's scope ([basic.scope.block]/3) */
node_index parser::for_range_declaration()
{
    size_t mark = begin_node();
    token_index first = position;
    attribute_specifier_seq();
    if (failed())
        return 0;
    decl_specifier_facts specifiers;
    decl_specifier_seq(specifiers, false);
    if (failed())
        return 0;
    if (!specifiers.node)
        return expected("a decl-specifier-seq", "stmt.iter");
    if (structured_binding_follows()) {
        /* [dcl.struct.bind] */
        structured_binding_identifiers();
        if (failed())
            return 0;
        return finish_node(syntax_kind::for_range_declaration, mark, first);
    }
    declarator_facts facts;
    declarator(declarator_form::concrete, facts);
    if (failed())
        return 0;
    if (facts.name && !facts.qualified_id)
        names.declare(facts.name, name_kind::other);
    return finish_node(syntax_kind::for_range_declaration, mark, first);
}

} // namespace cplusplus
