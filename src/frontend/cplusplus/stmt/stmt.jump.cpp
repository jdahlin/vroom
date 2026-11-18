/*
 * stmt.jump.cpp: [stmt.jump] Jump statements, with [stmt.break],
 * [stmt.cont], [stmt.return] and [stmt.goto].
 *
 *     jump-statement:
 *         break ;
 *         continue ;
 *         return expr-or-braced-init-list_opt ;
 *         coroutine-return-statement
 *         goto identifier ;
 *
 * coroutine-return-statement is stmt.return.coroutine.cpp's.
 */
#include "frontend/cplusplus/sema.h"
#include "stmt.ambig.h"

namespace cplusplus
{

node_index parser::jump_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    standard_keyword k = keyword_of(peek());
    add_token();
    if (k == standard_keyword::return_ &&
        !at(preprocessing_op_or_punc::SEMICOLON)) {
        /* expr-or-braced-init-list ([dcl.init]) */
        if (at(preprocessing_op_or_punc::LEFT_BRACE)) {
            if (list_initialization_missing())
                return 0;
            braced_init_list();
        } else {
            expression();
        }
        if (failed())
            return 0;
    } else if (k == standard_keyword::goto_) {
        if (at(preprocessing_op_or_punc::STAR)) {
            /* [ext:gnu_labels_as_values] */
            if (tokens.files.behavior.has(extension::gnu_labels_as_values))
                return gnu_computed_goto(mark, first);
            return beyond_this_step();
        }
        if (!at_identifier())
            return expected("an identifier after goto", "stmt.goto");
        add_token();
    }
    if (!accept(preprocessing_op_or_punc::SEMICOLON))
        return expected("';' after the jump-statement", "stmt.jump");
    return finish_node(syntax_kind::jump_statement, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * [stmt.jump]/1:
 *
 *     jump-statement:
 *         break ;
 *         continue ;
 *         return expr-or-braced-init-list_opt ;
 *         coroutine-return-statement
 *         goto identifier ;
 */
analysis_result semantic_analysis::jump_statement(node_index n, uint32_t &built)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    if (!node.child_count || !children[0].is_token())
        return not_supported(n, "stmt.jump");
    token_index at = children[0].index();
    switch (p.keyword_of(tokens[at])) {
    case standard_keyword::break_:
        /* [stmt.break]/1: "The break statement shall occur only in an
           iteration-statement or a switch statement" */
        if (!function_state.breakable)
            return ill_formed(n,
                              "the break statement shall occur only in an "
                              "iteration-statement or a switch statement",
                              "stmt.break");
        built = statement_node(semantic_kind::break_statement, at);
        return analysis_result::well_formed;
    case standard_keyword::continue_:
        /* [stmt.cont]/1: "The continue statement shall occur only in an
           iteration-statement" */
        if (!function_state.loops)
            return ill_formed(n,
                              "the continue statement shall occur only in an "
                              "iteration-statement",
                              "stmt.cont");
        built = statement_node(semantic_kind::continue_statement, at);
        return analysis_result::well_formed;
    case standard_keyword::goto_: {
        if (node.child_count != 3 || !children[1].is_token() ||
            tokens[children[1].index()].kind != token_kind::identifier)
            /* GNU's computed goto */
            return not_supported(n, "stmt.goto");
        goto_record g;
        g.name = tokens.identifier(children[1].index());
        g.point.scope = current_scope;
        g.point.at = children[0].index();
        g.node = n;
        function_state.gotos.append(g);
        built = statement_node(semantic_kind::goto_statement, at);
        semantics[built].value = g.name;
        return analysis_result::well_formed;
    }
    case standard_keyword::return_: {
        uint32_t operand = 0;
        analysis_result r = return_statement(n, operand);
        built = statement_node(semantic_kind::return_statement, at, operand);
        return r;
    }
    default:
        return not_supported(n, "stmt.jump");
    }
}

/*
 * [stmt.return]/2: "A return statement with no operand shall be used only
 * in a function whose return type is cv void, a constructor, or a
 * destructor.  A return statement with an operand of type void shall be
 * used only in a function whose return type is cv void.  A return statement
 * with any other operand shall be used only in a function whose return type
 * is not cv void; the return statement initializes the glvalue result or
 * prvalue result object of the (explicit or implicit) function call by
 * copy-initialization from the operand."  clang's -Wreturn-mismatch for
 * the first and the last, an error by default.
 */
analysis_result semantic_analysis::return_statement(node_index n,
                                                    uint32_t &operand_node)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    qualified_type returns = function_state.returns;
    bool returns_void = fundamental_of(returns) == fundamental_type::void_;
    if (node.child_count == 2) {
        if (returns_void)
            return analysis_result::well_formed;
        return extension_used(diagnostic_id::sema_return_without_value,
                              children[0].index(), nullptr);
    }
    if (node.child_count != 3)
        return not_supported(n, "stmt.return");
    syntax_child operand = children[1];
    initialized_value result;
    if (syntax_is(tree, operand, syntax_kind::braced_init_list)) {
        if (returns_void)
            return ill_formed(n,
                              "a return statement with any other operand "
                              "shall be used only in a function whose return "
                              "type is not cv void",
                              "stmt.return");
        qualified_type type = returns;
        analysis_result r =
            list_initialization(operand.index(), type, false, result);
        operand_node = result.node;
        return r;
    }
    /* [class.copy.elision]/3: "An implicitly movable entity is a variable
       of automatic storage duration that is either a non-volatile object
       or an rvalue reference to a non-volatile object type."  Returned by
       name it is an xvalue in C++23 and is first tried as an rvalue in
       C++11 to C++20; for a reference result that decides whether the
       initialization is well-formed, which analysis does not follow */
    type_kind result_kind = type_table[returns.unqualified()].kind;
    if (result_kind == type_kind::lvalue_reference ||
        result_kind == type_kind::rvalue_reference) {
        syntax_child named = operand;
        while (!named.is_token() &&
               tree[named.index()].kind == syntax_kind::primary_expression &&
               tree[named.index()].child_count == 3)
            named = tree.children(named.index())[1];
        if (named.is_token() &&
            tokens[named.index()].kind == token_kind::identifier) {
            entity_index e = lookup(tokens.identifier(named.index()));
            /* an lvalue reference is not implicitly movable */
            if (e && entity_table[e].automatic &&
                type_table[entity_table[e].type.unqualified()].kind !=
                    type_kind::lvalue_reference)
                return not_supported(n, "class.copy.elision");
        }
    }
    expression_meaning m;
    analysis_result r = expression(operand, m);
    if (r != analysis_result::well_formed)
        return r;
    /* a void operand, or (an extension) one in a function returning void,
       is evaluated and discarded */
    operand_node = m.node;
    bool void_operand = m.category == value_category::prvalue &&
                        fundamental_of(m.type) == fundamental_type::void_;
    if (returns_void) {
        if (void_operand)
            return analysis_result::well_formed;
        return extension_used(diagnostic_id::sema_return_with_value,
                              children[0].index(), nullptr);
    }
    if (void_operand)
        return ill_formed(n,
                          "a return statement with an operand of type void "
                          "shall be used only in a function whose return "
                          "type is cv void",
                          "stmt.return");
    r = initialize_with(n, m, returns, false, false, result);
    operand_node = result.node;
    return r;
}

} // namespace cplusplus
