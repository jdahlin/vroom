/*
 * except.spec.cpp: [except.spec] Exception specifications, as far as a
 * declarator needs them.
 *
 *     noexcept-specifier:
 *         noexcept ( constant-expression )
 *         noexcept
 */
#include "../cplusplus-internal.h"
#include "frontend/cplusplus/sema.h"

namespace cplusplus
{

node_index parser::noexcept_specifier()
{
    /* [temp.names]/3: a > in brackets nested in a template-argument-list
       is an operator */
    flag_set greater(greater_is_operator, true);
    size_t mark = begin_node();
    token_index first = position;
    add_token();
    if (accept(preprocessing_op_or_punc::LEFT_PARENTHESIS)) {
        constant_expression();
        if (failed())
            return 0;
        if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
            return expected("')' to close the noexcept-specifier",
                            "except.spec");
    }
    return finish_node(syntax_kind::noexcept_specifier, mark, first);
}

/*
 * C++14's [except.spec]/1:
 *
 *     dynamic-exception-specification:
 *         throw ( type-id-list_opt )
 *
 *     type-id-list:
 *         type-id ..._opt
 *         type-id-list , type-id ..._opt
 *
 * [diff.cpp14.except]/1: C++17 removed it, keeping throw ( ) as a
 * noexcept-specifier ("Remove dynamic exception specifications", P0003R5), and
 * [diff.cpp17.except]/1: C++20 removed that too ("Remove throw()
 * exception specification").  In those editions they are clang's
 * extension, P9's; throw ( ... ) is Microsoft's in every edition.  C++03 has no pack expansion: the
 * ... after a type-id is C++11's (variadic templates, P7's).
 */
node_index parser::dynamic_exception_specification()
{
    size_t mark = begin_node();
    token_index first = position;
    const implementation_defined_behavior &b = tokens.files.behavior;
    bool empty = at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
                 at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 2);
    if (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, 1) &&
        at(preprocessing_op_or_punc::ELLIPSIS, 2) &&
        at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 3))
        return beyond_this_step();
    if (!b.has(language_feature::dynamic_exception_specifications)) {
        /* clang's extension of the later editions (an error unless
           -Wno-dynamic-exception-spec) */
        if (!empty)
            return beyond_this_step();
        if (!b.has(language_feature::empty_throw_exception_specification))
            return beyond_this_step();
        add_token(); /* throw */
        add_token(); /* ( */
        add_token(); /* ) */
        return finish_node(syntax_kind::noexcept_specifier, mark, first);
    }
    add_token(); /* throw */
    if (!accept(preprocessing_op_or_punc::LEFT_PARENTHESIS))
        return expected("'(' after throw", "except.spec");
    if (!at(preprocessing_op_or_punc::RIGHT_PARENTHESIS)) {
        size_t list_mark = begin_node();
        token_index list_first = position;
        unsigned count = 0;
        for (;;) {
            type_id();
            if (failed())
                return 0;
            if (at(preprocessing_op_or_punc::ELLIPSIS))
                return beyond_this_step(); /* a pack expansion: P7 */
            count++;
            if (!accept(preprocessing_op_or_punc::COMMA))
                break;
        }
        if (count > 1)
            finish_node(syntax_kind::type_id_list, list_mark, list_first);
    }
    if (!accept(preprocessing_op_or_punc::RIGHT_PARENTHESIS))
        return expected("')' to close the dynamic-exception-specification",
                        "except.spec");
    return finish_node(syntax_kind::dynamic_exception_specification, mark,
                       first);
}

} // namespace cplusplus

namespace cplusplus
{

/*
 * [except.spec]/2: "The exception specification is either defined
 * implicitly, or defined explicitly by using a noexcept-specifier as a
 * suffix of a function declarator.  In a noexcept-specifier, the
 * constant-expression, if supplied, shall be a contextually converted
 * constant expression of type bool ([expr.const]); that constant
 * expression is the exception specification of the function type in which
 * the noexcept-specifier appears.  A ( token that follows noexcept is part
 * of the noexcept-specifier and does not commence an initializer.  The
 * noexcept-specifier noexcept without a constant-expression is equivalent
 * to the noexcept-specifier noexcept(true)."  [except.spec]/3, C++17's
 * [except.spec]/2: "The noexcept-specifier throw() is equivalent to
 * noexcept(true)"; it is one here (parser::dynamic_exception_specification).
 * /5: noexcept(true) is a non-throwing exception specification, and with
 * -fno-exceptions nothing analysis generates depends on it but the type.
 */
analysis_result semantic_analysis::noexcept_specifier(node_index n, bool member,
                                                      bool &non_throwing)
{
    const syntax_node &node = tree[n];
    const syntax_child *children = tree.children(n);
    non_throwing = true;
    if (node.child_count == 1 || node.child_count == 3)
        /* noexcept, throw ( ) */
        return analysis_result::well_formed;
    if (node.child_count != 4)
        return not_supported(n, "except.spec");
    if (member)
        /* [class.mem]/6: a member function's noexcept-specifier is a
           complete-class context, analysed with the class complete: not
           deferred yet */
        return not_supported(n, "except.spec");
    bool constant = false, value = false;
    analysis_result r =
        bool_constant_expression(n, children[2], constant, value);
    if (r != analysis_result::well_formed)
        return r;
    if (!constant)
        return ill_formed(n,
                          "in a noexcept-specifier, the constant-expression "
                          "shall be a contextually converted constant "
                          "expression of type bool",
                          "except.spec");
    non_throwing = value;
    return analysis_result::well_formed;
}

} // namespace cplusplus
