/*
 * stmt.ambig.h: [stmt.ambig] Ambiguity resolution, the part every file of
 * [stmt] instantiates (a member template of the parser).
 *
 * [stmt.ambig]/1: "An expression-statement with a function-style explicit
 * type conversion ([expr.type.conv]) as its leftmost subexpression can be
 * indistinguishable from a declaration where the first declarator starts
 * with a (. In those cases the statement is a declaration."  /2: "If the
 * statement cannot syntactically be a declaration, there is no ambiguity
 * ... The whole statement might need to be examined to determine whether
 * this is the case."  [stmt.pre]/7 says the same of a condition.
 *
 * So the declaration is tried first, to its end, and the expression only
 * when the tokens cannot be one.  /3: "The disambiguation is purely
 * syntactic; that is, the meaning of the names occurring in such a
 * statement, beyond whether they are type-names or not, is not generally
 * used": which is all reading_at() asks.  When it cannot tell, because a
 * name is one the parser cannot classify (declared in a class or a
 * template: P5, P7), the tokens are read both ways with such names taken
 * for type-names: a statement that is only an expression either way is
 * one; otherwise what it is depends on the names, and the parse fails as a
 * later step's.
 */
#ifndef VCC_CPLUSPLUS_STMT_AMBIG_H
#define VCC_CPLUSPLUS_STMT_AMBIG_H

#include "../cplusplus-internal.h"

namespace cplusplus
{

template <class Declaration, class Expression>
node_index parser::declaration_or_expression(reading r, Declaration declaration,
                                             Expression expression)
{
    if (r == reading::declaration)
        return declaration();
    if (r == reading::expression)
        return expression();
    failure_state as_declaration, as_expression;
    bool declaration_parses = false;
    if (r == reading::either) {
        tentative_parse attempt(*this);
        node_index n = declaration();
        if (!failed()) {
            attempt.commit();
            return n;
        }
        as_declaration = failure;
    } else {
        tentative_parse attempt(*this);
        flag_set as_types(unknown_names_are_types, true);
        declaration();
        declaration_parses = !failed();
        as_declaration = failure;
    }
    {
        tentative_parse attempt(*this);
        node_index n = expression();
        if (!failed() && !declaration_parses) {
            attempt.commit();
            return n;
        }
        as_expression = failure;
    }
    if (r == reading::unknown)
        unknown_name = true;
    if (declaration_parses || as_declaration.beyond_step ||
        as_expression.beyond_step)
        return beyond_this_step();
    /* neither reading: the one that went further says what is wrong */
    if (as_declaration.at > as_expression.at)
        failure = as_declaration;
    else
        failure = as_expression;
    return 0;
}

} // namespace cplusplus

#endif
