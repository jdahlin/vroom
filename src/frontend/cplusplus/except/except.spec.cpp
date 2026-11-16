/*
 * except.spec.cpp: [except.spec] Exception specifications, as far as a
 * declarator needs them.
 *
 *     noexcept-specifier:
 *         noexcept ( constant-expression )
 *         noexcept
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

node_index parser::noexcept_specifier()
{
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
