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
#include "frontend/cplusplus/sema.h"
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

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [stmt.iter]/1:
 *
 *     iteration-statement:
 *         while ( condition ) statement
 *         do statement while ( expression ) ;
 *         for ( init-statement condition_opt ; expression_opt ) statement
 *         for ( init-statement_opt for-range-declaration : for-range-initializer ) statement
 *
 * The range-based for is not analysed yet.
 */
analysis_result semantic_analysis::iteration_statement(node_index n,
                                                       uint32_t &built)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (!node.child_count || !children[0].is_token())
        return not_supported(n, "stmt.iter");
    standard_keyword k = p.keyword_of(tokens[children[0].index()]);
    entity_index outer = current_scope, statement_scope = 0;
    analysis_result result = analysis_result::well_formed;
    expression_meaning m;
    token_index at = children[0].index();
    uint32_t body = 0, init = 0, step_node = 0;
    if (k == standard_keyword::do_) {
        /* do statement while ( expression ) ; */
        if (node.child_count != 7)
            return not_supported(n, "stmt.do");
        function_state.loops++;
        function_state.breakable++;
        result = substatement(children[1], 0, body);
        function_state.loops--;
        function_state.breakable--;
        /* [stmt.do]/1: "The expression is contextually converted to bool;
           if that conversion is ill-formed, the program is ill-formed." */
        analysis_result r = expression(children[4], m);
        if (r == analysis_result::well_formed)
            r = contextual_bool(n, m);
        built = statement_node(semantic_kind::do_statement, at, body, m.node);
        return worse(result, r);
    }
    if (k == standard_keyword::while_) {
        if (node.child_count != 5)
            return not_supported(n, "stmt.while");
        if (syntax_is(tree, children[2], syntax_kind::condition))
            statement_scope = open_scope(0, false);
        result = condition(children[2], false, m);
    } else if (k == standard_keyword::for_) {
        if (node.child_count < 6)
            return not_supported(n, "stmt.for");
        for (uint32_t i = 0; i < node.child_count; i++)
            if (syntax_is_punctuator(tokens, children[i],
                                     preprocessing_op_or_punc::COLON) ||
                syntax_is(tree, children[i],
                          syntax_kind::for_range_declaration))
                return not_supported(n, "stmt.ranged");
        /* for ( init-statement condition_opt ; expression_opt ) statement:
           the init-statement ends with its own ; */
        syntax_child init_syntax = children[2], cond, step;
        bool has_cond = false, has_step = false;
        uint32_t i = 3;
        if (!syntax_is_punctuator(tokens, children[i],
                                  preprocessing_op_or_punc::SEMICOLON)) {
            cond = children[i++];
            has_cond = true;
        }
        if (i >= node.child_count ||
            !syntax_is_punctuator(tokens, children[i],
                                  preprocessing_op_or_punc::SEMICOLON))
            return not_supported(n, "stmt.for");
        i++;
        if (i < node.child_count &&
            !syntax_is_punctuator(
                tokens, children[i],
                preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
            step = children[i++];
            has_step = true;
        }
        if (i + 2 != node.child_count)
            return not_supported(n, "stmt.for");
        if (!syntax_is(tree, init_syntax, syntax_kind::expression_statement) ||
            (has_cond && syntax_is(tree, cond, syntax_kind::condition)))
            statement_scope = open_scope(0, false);
        bool claimable = declarations_claimable;
        declarations_claimable = false;
        result = statement(init_syntax, init);
        declarations_claimable = claimable;
        if (has_cond)
            result = worse(result, condition(cond, false, m));
        if (has_step) {
            expression_meaning e;
            result = worse(result, expression(step, e));
            step_node = e.node;
        }
    } else {
        return not_supported(n, "stmt.iter");
    }
    function_state.loops++;
    function_state.breakable++;
    result = worse(result, substatement(children[node.child_count - 1],
                                        statement_scope, body));
    function_state.loops--;
    function_state.breakable--;
    current_scope = outer;
    if (k == standard_keyword::while_)
        built =
            statement_node(semantic_kind::while_statement, at, m.node, body);
    else
        built = statement_node(semantic_kind::for_statement, at, init, m.node,
                               step_node, body);
    return result;
}

} // namespace cplusplus
