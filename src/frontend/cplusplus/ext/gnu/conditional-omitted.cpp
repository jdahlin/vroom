/*
 * conditional-omitted.cpp: [ext:gnu_conditional_omitted] (GCC manual 6.8,
 * Conditionals with Omitted Operands): "The middle operand in a
 * conditional expression may be omitted.  Then if the first operand is
 * nonzero, its value is the value of the conditional expression."
 *
 *     conditional-expression:
 *         logical-or-expression ? : assignment-expression
 *
 * "Therefore, the expression x ? : y has the value of x if that is
 * nonzero; otherwise, the value of y.  This example is perfectly
 * equivalent to x ? x : y.  In this simple case, the ability to omit the
 * middle operand is not especially useful.  When it becomes useful is when
 * the first operand does, or may (if it is a macro argument), contain a
 * side effect.  Then repeating the operand in the middle would perform the
 * side effect twice.  Omitting the middle operand uses the value already
 * computed without the undesirable effects of recomputing it."
 *
 * The first operand is the second operand of [expr.cond] too (clang's
 * opaque value): when x and y are lvalues of one type, so is the result;
 * else x is converted as the operand of a unary operator is (an integral
 * type or an unscoped enumeration promoted: char ? : char is an int).
 * The semantic node is a conditional with no b: a is x converted to bool,
 * d is x converted as the second operand is (the same operand, which
 * lowering evaluates once through d), c is y.
 *
 * Analysed for an arithmetic, enumeration or pointer first operand; a class
 * one (converted to bool by a function) is not.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

/* at the ':' after the '?' of the conditional-expression that began at
   mark and first */
node_index parser::gnu_conditional_omitted(size_t mark, token_index first,
                                           bool *conditional)
{
    add_token(); /* : */
    assignment_expression();
    if (failed())
        return 0;
    if (conditional)
        *conditional = true;
    return finish_node(syntax_kind::conditional_expression, mark, first);
}

analysis_result
semantic_analysis::gnu_conditional_omitted(SyntaxRef n, expression_meaning &m)
{
    if (n.size() != 4 || !n[1].is_token() || !n[2].is_token())
        return not_supported(n, "ext.gnu");
    expression_meaning e2, e3;
    VTRY(r, expression(n[0], e2));
    VTRY_SET(r, expression(n[3], e3));
    /* clang: the first operand is converted, before it is saved as the
       second, as an operand of a unary operator is (lvalue-to-rvalue,
       array-to-pointer, function-to-pointer and the integral promotions)
       except when this operand and the third are glvalues of one type */
    bool lvalues = e2.category != value_category::prvalue &&
                   e3.category != value_category::prvalue && e2.type == e3.type;
    if (!lvalues) {
        VTRY_SET(r, operand_value(n, e2));
        if (!promote_enumeration(e2))
            promote(e2);
    }
    const struct type &first = type_table[e2.type.unqualified()];
    if (first.kind != type_kind::enumeration && !is_arithmetic(e2.type) &&
        !is_pointer(e2.type)) {
        r = not_supported(n, "ext.gnu");
        name_unsupported_detail(n[1].token(),
                                "with a first operand that is not arithmetic, "
                                "an enumeration or a pointer is not analysed");
        return r;
    }
    /* the value is also the condition */
    expression_meaning c = e2;
    VTRY_SET(r, contextual_bool(n, c));
    VTRY_SET(r, conditional_operands(n, c, e2, e3, m));
    semantic_node &made = semantics[m.node];
    made.d = made.b;
    made.b = 0;
    return analysis_result::well_formed;
}

} // namespace cplusplus
