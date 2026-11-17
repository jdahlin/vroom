/*
 * compound_literal.cpp: [ext:gnu_compound_literals] (GCC manual 6.30,
 * Compound Literals): C99's compound literal (C17 6.5.2.5), which GCC and
 * clang take in C++ too:
 *
 *     postfix-expression:
 *         gnu-compound-literal
 *
 *     gnu-compound-literal:
 *         ( type-id ) braced-init-list
 *
 * "In C, a compound literal designates an unnamed object with static or
 * automatic storage duration.  In C++, a compound literal designates a
 * temporary object that only lives until the end of its full-expression."
 * Where the standard has a cast-expression, ( type-id ) followed by a '{'
 * is one (dcl.ambig.res.cpp), and the postfix-expression's suffixes may
 * follow it: (struct s){1, 2}.x.
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

/* at the '{' after ( type-id ), whose node begins at mark and first */
node_index parser::gnu_compound_literal(size_t mark, token_index first)
{
    if (list_initialization_missing())
        return 0;
    braced_init_list();
    if (failed())
        return 0;
    node_index n = finish_node(syntax_kind::gnu_compound_literal, mark, first);
    return postfix_expression_suffixes(mark, first, n);
}

} // namespace cplusplus
