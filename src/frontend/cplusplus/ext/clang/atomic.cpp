/*
 * atomic.cpp: [ext:clang_atomic] (clang Language Extensions, C11 _Atomic
 * in C++): C's atomic type specifier, which libc++'s
 * __atomic/support/c11.h declares std::atomic's object with.
 *
 *     simple-type-specifier:
 *         clang-atomic-type-specifier
 *
 *     clang-atomic-type-specifier:
 *         _Atomic ( type-id )
 *
 * The type has C11's atomic layout (unary-expression.cpp, atomic_layout).  An
 * expression of atomic type (its loads, stores and the __c11_atomic
 * builtins on it) is not analysed yet; C's _Atomic qualifier (no '(')
 * is not parsed.
 */
#include "../../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::clang_atomic_type_specifier()
{
    if (!at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1))
        return beyond_this_step();
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* _Atomic */
    add_token(); /* ( */
    flag_set greater(greater_is_operator, true);
    type_id();
    if (failed())
        return 0;
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to end the atomic type specifier", "ext.clang");
    return finish_node(syntax_kind::clang_atomic_type_specifier, mark, first);
}

/* [ext:clang_atomic] _Atomic(T): clang's rules, as for C (C23 6.7.3.5/3:
   "The type name in an atomic type specifier shall not refer to an array
   type, a function type, an atomic type, or a qualified type") */
analysis_result semantic_analysis::clang_atomic_type(SyntaxRef n,
                                                     qualified_type &type)
{
    qualified_type value;
    analysis_result r = type_id(n[2], value);
    if (r != analysis_result::well_formed)
        return r;
    type_kind k = type_table[value.unqualified()].kind;
    if (value.cv() || k == type_kind::array || k == type_kind::function ||
        k == type_kind::atomic || k == type_kind::lvalue_reference ||
        k == type_kind::rvalue_reference)
        return ill_formed(n,
                          "_Atomic cannot be applied to an array, function, "
                          "reference, atomic or qualified type",
                          "ext.clang");
    type = qualified_type::make(type_table.atomic_of(value), cv_none);
    return analysis_result::well_formed;
}

} // namespace cplusplus
