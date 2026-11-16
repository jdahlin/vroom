/*
 * stmt.select.cpp: [stmt.select] Selection statements, with [stmt.if] and
 * [stmt.switch].
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
#include "stmt.ambig.h"

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

} // namespace cplusplus
