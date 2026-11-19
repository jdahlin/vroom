/*
 * selection-statement.cpp: [stmt.select] Selection statements, with [stmt.if]
 * and [stmt.switch].
 *
 *     selection-statement:
 *         if constexpr_opt ( init-statement_opt condition ) statement
 *         if constexpr_opt ( init-statement_opt condition ) statement else statement
 *         switch ( init-statement_opt condition ) statement
 *
 * [stmt.select]/1: "In clause [stmt.stmt], the term substatement refers to
 * the contained statement or statements": an else belongs to the innermost
 * if that has none ([stmt.if]/1's "the second substatement").  The names
 * the init-statement and the condition declare are local to the statement
 * ([basic.scope.block]/3).  The init-statement is P0305R1's (C++17) and if
 * constexpr P0292R2's; before them both are [ext.clang] clang's extensions,
 * P9's, as is P1938R3's if consteval (C++23).
 */
#include "frontend/cplusplus/sema.h"
#include "ambiguity-resolution.h"

namespace cplusplus
{

node_index parser::selection_statement()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool if_ = at(standard_keyword::if_);
    add_token();
    if (if_ && at(standard_keyword::constexpr_)) {
        if (!b.has(language_feature::constexpr_if))
            return beyond_this_step();
        add_token();
    } else if (if_ && (at(standard_keyword::consteval_) ||
                       at(preprocessing_op_or_punc::EXCLAMATION))) {
        return beyond_this_step();
    }
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        if (if_)
            return expected("'(' after if", "stmt.if");
        return expected("'(' after switch", "stmt.switch");
    }
    declarative_region statement_scope(names, scope_kind::block);
    if (scan_parenthesized(*this).semicolons) {
        if (!b.has(language_feature::selection_init_statement))
            return beyond_this_step();
        init_statement();
        if (failed())
            return 0;
    }
    condition(preprocessing_op_or_punc::RIGHT_PARENTHESIS);
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' after the condition", "stmt.select");
    substatement();
    if (failed())
        return 0;
    if (if_ && accept(standard_keyword::else_)) {
        substatement();
        if (failed())
            return 0;
    }
    return finish_node(syntax_kind::selection_statement, mark, first);
}

/* ---- semantic analysis ---------------------------------------------- */

/*
 * The parts of a selection-statement or iteration-statement between its
 * parentheses: the init-statement and condition of if and switch, or
 * for's init-statement, condition and expression.
 */
namespace
{

struct statement_parts {
    SyntaxElement init, condition, expression, body, otherwise;
    bool has_init = false, has_condition = false, has_expression = false,
         has_otherwise = false;
};

} // namespace

/*
 * [stmt.select]/1:
 *
 *     selection-statement:
 *         if constexpr_opt ( init-statement_opt condition ) statement
 *         if constexpr_opt ( init-statement_opt condition ) statement else statement
 *         switch ( init-statement_opt condition ) statement
 *
 * [stmt.if]/3 and [stmt.switch]/5: an init-statement's names are local to
 * the statement, as the condition's are ([basic.scope.block]/3).
 */
analysis_result semantic_analysis::selection_statement(SelectionStatement n,
                                                       uint32_t &built)
{
    if (n.size() < 5 || !n[0].is_token() ||
        !n[1].is(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        /* if constexpr, if consteval */
        return not_supported(n, "stmt.if");
    bool is_switch =
        p.keyword_of(tokens[n[0].token()]) == standard_keyword::switch_;
    statement_parts parts;
    uint32_t i = 2;
    owned_array<SyntaxElement> inside;
    for (; i < n.size(); i++) {
        if (n[i].is(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            break;
        inside.append(n[i]);
    }
    if (i + 1 >= n.size() || inside.empty() || inside.size() > 2)
        return not_supported(n, "stmt.select");
    if (inside.size() == 2) {
        parts.has_init = true;
        parts.init = inside[0];
    }
    parts.condition = inside[inside.size() - 1];
    parts.body = n[i + 1];
    if (i + 3 < n.size()) {
        parts.has_otherwise = true;
        parts.otherwise = n[i + 3];
    }
    entity_index outer = current_scope, statement_scope = 0;
    if ((parts.has_init && !parts.init.is(syntax_kind::expression_statement)) ||
        parts.condition.is(syntax_kind::condition))
        statement_scope = open_scope(0, false);
    analysis_result result = analysis_result::well_formed;
    uint32_t init = 0, body = 0, otherwise = 0;
    if (parts.has_init) {
        bool claimable = declarations_claimable;
        declarations_claimable = false;
        result = statement(parts.init, init);
        declarations_claimable = claimable;
    }
    expression_meaning m;
    analysis_result c = condition(parts.condition, is_switch, m);
    result = worse(result, c);
    token_index at = n[0].token();
    if (!is_switch) {
        result = worse(result, substatement(parts.body, statement_scope, body));
        if (parts.has_otherwise)
            result = worse(result, substatement(parts.otherwise,
                                                statement_scope, otherwise));
        current_scope = outer;
        built = statement_node(semantic_kind::if_statement, at, m.node, body,
                               otherwise, init);
        return result;
    }
    switch_record sw;
    if (c == analysis_result::well_formed)
        sw.type = fundamental_of(m.type);
    sw.point.scope = current_scope;
    sw.point.at = n[i].token();
    sw.first_value = function_state.case_values.size();
    function_state.switches.append(sw);
    function_state.breakable++;
    result = worse(result, substatement(parts.body, statement_scope, body));
    function_state.breakable--;
    function_state.case_values.resize(sw.first_value);
    function_state.switches.resize(function_state.switches.size() - 1);
    current_scope = outer;
    built = statement_node(semantic_kind::switch_statement, at, m.node, body, 0,
                           init);
    return result;
}

} // namespace cplusplus
