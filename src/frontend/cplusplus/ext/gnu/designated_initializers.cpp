/*
 * designated_initializers.cpp: [ext:gnu_designated_initializers] (GCC
 * manual 6.29, Designated Initializers): C99's designators (C17 6.7.9),
 * which GCC and clang take in C++ beyond C++20's designated-initializer-list
 * ([dcl.init]/1: a list of ". identifier" clauses only), with GNU's ranges
 * and its obsolete "fieldname:":
 *
 *     braced-init-list:
 *         { gnu-initializer-list ,_opt }
 *
 *     gnu-initializer-list:
 *         gnu-designation_opt initializer-clause
 *         gnu-initializer-list , gnu-designation_opt initializer-clause
 *
 *     gnu-designation:
 *         gnu-designator-list =
 *         gnu-designator-list
 *         identifier :
 *
 *     gnu-designator-list:
 *         designator
 *         gnu-designator-list designator
 *
 *     designator:
 *         [ constant-expression ]
 *         [ constant-expression ... constant-expression ]
 *
 * A gnu-designator-list without = only before a braced-init-list (C++20's
 * ".a{1}").  A braced-init-list is one of these when the standard's lists
 * cannot have it: a designator in an edition before C++20, a designator
 * other than ". identifier", more than one designator, or clauses with and
 * without one.  A '[' begins a designator when =, '.' or '[' follows its
 * group (otherwise a lambda-expression's lambda-introducer).
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

/* the designators of a clause at `ahead`: how many tokens they take (0: no
   designation), whether they are only C++20's ". identifier" */
static unsigned designation_length(parser &p, unsigned ahead, bool &standard)
{
    standard = true;
    if (p.at_identifier(ahead) &&
        p.at(preprocessing_op_or_punc::COLON, ahead + 1)) {
        standard = false;
        return 2;
    }
    unsigned k = ahead, count = 0;
    for (;;) {
        if (p.at(preprocessing_op_or_punc::DOT, k) && p.at_identifier(k + 1)) {
            k += 2;
        } else if (p.at(preprocessing_op_or_punc::LEFT_BRACKET, k)) {
            unsigned end = p.group_end(k);
            if (!count && !p.at(preprocessing_op_or_punc::EQUAL, end) &&
                !p.at(preprocessing_op_or_punc::DOT, end) &&
                !p.at(preprocessing_op_or_punc::LEFT_BRACKET, end))
                return 0; /* a lambda-introducer */
            standard = false;
            k = end;
        } else {
            break;
        }
        count++;
    }
    if (count > 1)
        standard = false;
    return k - ahead;
}

/* at the first token after a braced-init-list's '{' */
bool parser::gnu_initializer_list_needed()
{
    if (!tokens.files.behavior.has(extension::gnu_designated_initializers))
        return false;
    bool designated = false, plain = false, standard_only = true;
    bool closed = false;
    for (unsigned k = 0; !closed;) {
        bool standard = true;
        unsigned length = designation_length(*this, k, standard);
        if (length) {
            designated = true;
            standard_only = standard_only && standard;
        } else {
            plain = true;
        }
        /* to the clause's end: a ',' or the '}' outside brackets */
        k += length;
        while (!closed && !at(preprocessing_op_or_punc::COMMA, k)) {
            if (tokens.at_end(position + k) ||
                at(preprocessing_op_or_punc::RIGHT_BRACE, k)) {
                closed = true;
            } else {
                unsigned end = group_end(k);
                if (end == k)
                    end++;
                k = end;
            }
        }
        k++;
        if (at(preprocessing_op_or_punc::RIGHT_BRACE, k))
            closed = true;
    }
    if (!designated)
        return false;
    return plain || !standard_only ||
           !tokens.files.behavior.has(
               language_feature::designated_initializers);
}

node_index parser::gnu_initializer_list()
{
    size_t mark = begin_node();
    token_index first = position;
    for (;;) {
        size_t designation_mark = begin_node();
        token_index designation_first = position;
        bool designation = false;
        if (at_identifier() && at(preprocessing_op_or_punc::COLON, 1)) {
            add_token();
            add_token();
            designation = true;
        } else {
            for (;;) {
                size_t designator_mark = begin_node();
                token_index designator_first = position;
                if (at(preprocessing_op_or_punc::DOT) && at_identifier(1)) {
                    add_token();
                    add_token();
                } else if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                           (designation ||
                            at(preprocessing_op_or_punc::EQUAL, group_end(0)) ||
                            at(preprocessing_op_or_punc::DOT, group_end(0)) ||
                            at(preprocessing_op_or_punc::LEFT_BRACKET,
                               group_end(0)))) {
                    add_token();
                    constant_expression();
                    if (failed())
                        return 0;
                    if (accept(preprocessing_op_or_punc::ELLIPSIS)) {
                        constant_expression();
                        if (failed())
                            return 0;
                    }
                    if (!accept(preprocessing_op_or_punc::RIGHT_BRACKET))
                        return expected("']' to close the designator",
                                        "ext.gnu");
                } else {
                    break;
                }
                finish_node(syntax_kind::designator, designator_mark,
                            designator_first);
                designation = true;
            }
            if (designation) {
                if (!accept(preprocessing_op_or_punc::EQUAL) &&
                    !at(preprocessing_op_or_punc::LEFT_BRACE))
                    return expected("'=' or a braced-init-list after the "
                                    "designator",
                                    "ext.gnu");
            }
        }
        if (designation)
            finish_node(syntax_kind::gnu_designation, designation_mark,
                        designation_first);
        initializer_clause();
        if (failed())
            return 0;
        if (!at(preprocessing_op_or_punc::COMMA) ||
            at(preprocessing_op_or_punc::RIGHT_BRACE, 1))
            break;
        add_token();
    }
    return finish_node(syntax_kind::gnu_initializer_list, mark, first);
}

} // namespace cplusplus
