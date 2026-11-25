/*
 * va-arg.cpp: [ext:gnu_va_arg] (GCC manual 6.55, <cstdarg>'s va_arg
 * ([cstdarg.syn]): "The va_arg macro ... the expression has the type of
 * the second argument").  The builtin form is the primitive <stdarg.h>
 * expands va_arg to:
 *
 *     primary-expression:
 *         gnu-va-arg-expression
 *
 *     gnu-va-arg-expression:
 *         __builtin_va_arg ( assignment-expression , type-id )
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::gnu_va_arg_expression()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* __builtin_va_arg */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after __builtin_va_arg", "ext.gnu");
    flag_set greater(greater_is_operator, true);
    assignment_expression();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::COMMA))
        return expected("',' after the va_list", "ext.gnu");
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end __builtin_va_arg", "ext.gnu");
    return finish_node(syntax_kind::gnu_va_arg_expression, mark, first);
}

bool semantic_analysis::is_va_list(qualified_type t, uint32_t &cv) const
{
    cv = t.cv();
    if (fundamental_of(t) == fundamental_type::builtin_va_list)
        return true;
    const struct type &r = type_table[t.unqualified()];
    if (r.kind != type_kind::array && r.kind != type_kind::pointer)
        return false;
    if (fundamental_of(r.operand) != fundamental_type::builtin_va_list_tag)
        return false;
    cv = r.operand.cv();
    return true;
}

/* [ext:gnu_va_arg] a prvalue of the type-id's type read from the va_list,
   an lvalue.  The type is one the caller's default argument promotions
   ([expr.call]/12) leave as it is: an integer of at least int's width, a
   double, long double or pointer.  clang's SemaBuiltinVAArg answers a
   promotable type with a warning and a trap in place of the read, which is
   not what this lowering does: those are not supported; so are class and
   enumeration types, whose passing through an ellipsis is
   conditionally-supported ([expr.call]/12) */
analysis_result semantic_analysis::gnu_va_arg(SyntaxRef n,
                                              expression_meaning &m)
{
    expression_meaning list;
    VTRY(r, expression(n[2], list));
    qualified_type t;
    VTRY_SET(r, type_id(n[4], t));
    if (depends_on_template_parameter(t) ||
        depends_on_template_parameter(list.type))
        return not_supported_because(n, "temp.dep.constexpr",
                                     "va_arg with a dependent type or va_list");
    uint32_t list_cv = 0;
    if (!is_va_list(list.type, list_cv))
        return ill_formed(n,
                          "first argument to 'va_arg' is of type that is not "
                          "'va_list'",
                          "ext.gnu");
    if (list.category != value_category::lvalue)
        return not_supported_because(
            n, "ext.gnu", "va_arg on a va_list that is not an lvalue");
    /* the va_list is read and advanced: clang's "binding reference of type
       '__builtin_va_list' to value of type 'const __builtin_va_list' drops
       'const' qualifier" */
    if (list_cv & cv_const)
        return ill_formed(n, "the va_list of va_arg shall not be const",
                          "ext.gnu");
    t = qualified_type::make(t.unqualified(), cv_none);
    const implementation_defined_behavior &b = tokens.files.behavior;
    fundamental_type f = fundamental_of(t);
    bool kept =
        is_pointer(t) || f == fundamental_type::double_ ||
        f == fundamental_type::long_double ||
        (fundamental_type_is_integral(f) && f != fundamental_type::bool_ &&
         fundamental_type_width(b, f) >= b.int_width);
    if (!kept)
        return not_supported_because(n, "ext.gnu",
                                     "va_arg of a type other than a pointer, "
                                     "wide integer, double or long double");
    m = expression_meaning();
    m.type = t;
    m.category = value_category::prvalue;
    m.evaluation = constness::not_constant;
    m.node =
        expression_node(semantic_kind::va_arg_, m, n.first_token(), list.node);
    return analysis_result::well_formed;
}

} // namespace cplusplus
