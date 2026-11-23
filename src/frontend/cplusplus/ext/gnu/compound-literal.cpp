/*
 * compound-literal.cpp: [ext:gnu_compound_literals] (GCC manual 6.30,
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
 * is one (ambiguity-resolution.cpp), and the postfix-expression's suffixes may
 * follow it: (struct s){1, 2}.x.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

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

/* the vector or scalar type's object initialized by the braced list, an
   lvalue in C and here too; the classes and arrays go with their rules of
   [dcl.init.list] and are not analysed */
analysis_result semantic_analysis::gnu_compound_literal(SyntaxRef n,
                                                        expression_meaning &m)
{
    if (n.size() != 4 || !n[1].is(syntax_kind::type_id) ||
        !n[3].is(syntax_kind::braced_init_list))
        return not_supported(n, "ext.gnu");
    qualified_type to;
    VTRY(r, type_id(n[1], to));
    if (type_table[to.unqualified()].kind != type_kind::vector)
        return not_supported(n, "ext.gnu");
    initialized_value result;
    VTRY_SET(r, list_initialization(n[3], to, true, result));
    m = expression_meaning();
    m.type = to;
    m.category = value_category::lvalue;
    m.evaluation = constness::not_constant;
    m.beyond_integral_constant = true;
    m.node = expression_node(semantic_kind::compound_literal, m,
                             n.first_token(), result.node);
    return analysis_result::well_formed;
}

} // namespace cplusplus
