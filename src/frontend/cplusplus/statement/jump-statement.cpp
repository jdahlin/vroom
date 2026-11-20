/*
 * jump-statement.cpp: [stmt.jump] Jump statements, with [stmt.break],
 * [stmt.cont], [stmt.return] and [stmt.goto].
 *
 *     jump-statement:
 *         break ;
 *         continue ;
 *         return expr-or-braced-init-list_opt ;
 *         coroutine-return-statement
 *         goto identifier ;
 *
 * coroutine-return-statement is coroutine-return-statement.cpp's.
 */
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

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
analysis_result semantic_analysis::jump_statement(JumpStatement n,
                                                  uint32_t &built)
{
    if (!n.size() || !n[0].is_token())
        return not_supported(n, "stmt.jump");
    token_index at = n[0].token();
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
        if (n.size() != 3 || !n[1].is_identifier())
            /* GNU's computed goto */
            return not_supported(n, "stmt.goto");
        goto_record g;
        g.name = tokens.identifier(n[1].token());
        g.point.scope = current_scope;
        g.point.at = n[0].token();
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
analysis_result semantic_analysis::return_statement(SyntaxRef n,
                                                    uint32_t &operand_node)
{
    qualified_type returns = function_state.returns;
    bool returns_void = fundamental_of(returns) == fundamental_type::void_;
    if (function_state.deducing && n.size() == 2) {
        /* [dcl.type.auto.deduct]/2: "If the return statement has no
           operand, then E is void()" */
        VTRY(r, deduce_return(n, nullptr, false));
        returns_void = true;
    }
    if (function_state.deducing && n.size() == 3 &&
        n[1].is(syntax_kind::braced_init_list))
        /* [dcl.type.auto.deduct]/3 */
        return ill_formed(n,
                          "if the deduction is for a return statement and E "
                          "is a braced-init-list, the program is ill-formed",
                          "dcl.type.auto.deduct");
    if (n.size() == 2) {
        if (returns_void)
            return analysis_result::well_formed;
        return extension_used(diagnostic_id::sema_return_without_value,
                              n[0].token(), nullptr);
    }
    if (n.size() != 3)
        return not_supported(n, "stmt.return");
    SyntaxElement operand = n[1];
    initialized_value result;
    if (operand.is(syntax_kind::braced_init_list)) {
        if (returns_void)
            return ill_formed(n,
                              "a return statement with any other operand "
                              "shall be used only in a function whose return "
                              "type is not cv void",
                              "stmt.return");
        qualified_type type = returns;
        analysis_result r = list_initialization(operand, type, false, result);
        operand_node = result.node;
        return r;
    }
    /* [class.copy.elision]/3: "An implicitly movable entity is a variable
       of automatic storage duration that is either a non-volatile object
       or an rvalue reference to a non-volatile object type."  Returned by
       name it is an xvalue in C++23 and is first tried as an rvalue in
       C++11 to C++20; for a reference result that decides whether the
       initialization is well-formed, which analysis does not follow */
    expression_meaning m;
    bool decltype_auto = false;
    if (function_state.deducing &&
        contains_placeholder(function_state.declared_returns, &decltype_auto) &&
        decltype_auto && implicitly_movable(operand))
        /* decltype(auto) of an implicitly movable entity: an xvalue in
           C++23 (P2266R3) */
        return not_supported(n, "class.copy.elision");
    if (function_state.deducing) {
        analysis_result r = expression(operand, m);
        if (r == analysis_result::well_formed)
            VTRY_SET(r, deduce_return(n, &m, names_entity(operand)));
        returns = function_state.returns;
        returns_void = fundamental_of(returns) == fundamental_type::void_;
    }
    type_kind result_kind = type_table[returns.unqualified()].kind;
    entity_index movable = implicitly_movable(operand);
    if (movable && (result_kind == type_kind::lvalue_reference ||
                    result_kind == type_kind::rvalue_reference))
        return not_supported(n, "class.copy.elision");
    /* [over.over]/1: "the return value of a function" */
    analysis_result r = analysis_result::well_formed;
    if (!function_state.deducing)
        VTRY_SET(r, target_expression(operand, returns, m));
    const struct type &object = type_table[m.type.unqualified()];
    if (movable && m.category == value_category::lvalue &&
        object.kind == type_kind::class_type &&
        declares_move_constructor(object.entity)) {
        /* the first overload resolution, as an rvalue, finds the move
           constructor viable: it is chosen ([over.ics.rank]/3.2.3), deleted
           or not.  For another class (a converting constructor), C++11 to
           C++17 take the copy unless the chosen constructor's parameter is
           an rvalue reference to the object's type, C++20 does not: not
           analysed */
        if (m.type.unqualified() != returns.unqualified())
            return not_supported(n, "class.copy.elision");
        m.category = value_category::xvalue;
    } else if (movable && m.category == value_category::lvalue &&
               object.kind == type_kind::class_type &&
               tokens.files.behavior.has(language_feature::rvalue_references) &&
               has_implicit_conversion_functions(object.entity)) {
        /* the first overload resolution, as an rvalue, may find a
           converting constructor of the returned class viable by a
           conversion function of the operand's (and its parameter is
           not an rvalue reference to the object's type, which C++11 to
           C++17 want): not analysed */
        return not_supported(n, "class.copy.elision");
    } else if (movable && m.category == value_category::lvalue &&
               object.kind == type_kind::class_type &&
               tokens.files.behavior.has(language_feature::rvalue_references) &&
               !copies_bytes(object.entity) &&
               !declares_copy_constructor(object.entity)) {
        /* the move constructor the class may declare implicitly
           ([class.copy.ctor]/8), a function other than its copy
           constructor: not analysed */
        return not_supported(n, "class.copy.elision");
    }
    /* a void operand, or (an extension) one in a function returning void,
       is evaluated and discarded */
    operand_node = m.node;
    bool void_operand = m.category == value_category::prvalue &&
                        fundamental_of(m.type) == fundamental_type::void_;
    if (returns_void) {
        if (void_operand)
            return analysis_result::well_formed;
        return extension_used(diagnostic_id::sema_return_with_value,
                              n[0].token(), nullptr);
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

/*
 * [class.copy.elision]/3: "An implicitly movable entity is a variable of
 * automatic storage duration that is either a non-volatile object or an
 * rvalue reference to a non-volatile object type."  The entity an operand
 * names by an id-expression (in parentheses or not), or none.
 */
entity_index semantic_analysis::implicitly_movable(SyntaxElement operand)
{
    SyntaxElement named = operand;
    while (!named.is_token() && named.is(syntax_kind::primary_expression) &&
           named.size() == 3)
        named = named[1];
    if (!named.is_token() ||
        tokens[named.token()].kind != token_kind::identifier)
        return 0;
    entity_index e = lookup(tokens.identifier(named.token()));
    if (!e || !entity_table[e].automatic)
        return 0;
    qualified_type t = entity_table[e].type;
    const struct type &k = type_table[t.unqualified()];
    if (k.kind == type_kind::lvalue_reference)
        return 0;
    if (k.kind == type_kind::rvalue_reference)
        t = k.operand;
    if (t.cv() & cv_volatile)
        return 0;
    return e;
}

} // namespace cplusplus
