/*
 * template-id.cpp: [temp.names] Names of template specializations.
 *
 *     simple-template-id:
 *         template-name < template-argument-list_opt >
 *
 *     template-id:
 *         simple-template-id
 *         operator-function-id < template-argument-list_opt >
 *         literal-operator-id < template-argument-list_opt >
 *
 *     template-name:
 *         identifier
 *
 *     template-argument-list:
 *         template-argument ..._opt
 *         template-argument-list , template-argument ..._opt
 *
 *     template-argument:
 *         constant-expression
 *         type-id
 *         id-expression
 *
 * Where a name followed by < is a template-id is decided where the name is
 * (decl-specifier.cpp, id-expression.cpp, declarator.cpp, base-clause.cpp,
 * ...), by [temp.names]/2 and /4 through names_template(); this file is the
 * argument list and its end.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [temp.names]/2: "A name is considered to refer to a template if name
 * lookup finds a template-name or an overload set that contains a function
 * template. A name is also considered to refer to a template if it is an
 * unqualified-id followed by a < and name lookup either finds one or more
 * functions or finds nothing." (the second sentence is P0846R0, C++20).
 * /4: after the keyword template, "a < token is always assumed to introduce
 * a template-argument-list".
 */
bool parser::names_template(name_kind kind, bool qualified,
                            bool template_keyword)
{
    if (template_keyword || is_template_kind(kind))
        return true;
    return !qualified &&
           (kind == name_kind::function || kind == name_kind::not_declared) &&
           tokens.files.behavior.has(
               language_feature::function_names_before_less);
}

name_kind parser::declared_kind(name_kind plain)
{
    if (template_scope != names.declaring_scope())
        return plain;
    /* [temp.pre]/1: "A template-declaration can appear only as a namespace
       scope or class scope declaration. Its declaration shall not be an
       export-declaration. In a function template declaration, the last
       component of the declarator-id shall not be a template-id." */
    switch (plain) {
    case name_kind::type_name:
        return name_kind::template_name;
    case name_kind::function:
        return name_kind::function_template;
    case name_kind::other:
        return name_kind::variable_template;
    default:
        return plain;
    }
}

/*
 * The lookahead after a template-argument-list's '>', without parsing it:
 * its '<' and '>' counted outside the brackets nested in it, a '>>' as two
 * '>' ([temp.names]/3) from C++11 on.  This is how far a name that is a
 * template-name reaches, for the decisions taken before the parse (what a
 * decl-specifier-seq or a statement starts with); the parse itself is
 * template_argument_list's.  A '>>' (or '>=', '>>=') whose first '>' ends
 * the group leaves its rest as the next token: the end is that token's
 * index, so that what follows the group is never taken for '::' or '<'.
 */
unsigned parser::angle_group_end(unsigned ahead)
{
    bool right_angles =
        tokens.files.behavior.has(language_feature::right_angle_brackets);
    unsigned depth = 0;
    for (unsigned i = ahead;; i++) {
        if (tokens.at_end(position + i))
            return 0;
        const token &t = peek(i);
        if (is_operator(t, preprocessing_op_or_punc::LESS)) {
            depth++;
        } else if (is_operator(t, preprocessing_op_or_punc::GREATER)) {
            if (!--depth)
                return i + 1;
        } else if (is_operator(t, preprocessing_op_or_punc::GREATER_EQUAL)) {
            if (!--depth)
                return i;
        } else if (right_angles &&
                   (is_operator(t, preprocessing_op_or_punc::GREATER_GREATER) ||
                    is_operator(
                        t, preprocessing_op_or_punc::GREATER_GREATER_EQUAL))) {
            if (depth == 1)
                return i;
            if (depth == 2) {
                if (is_operator(t, preprocessing_op_or_punc::GREATER_GREATER))
                    return i + 1;
                return i;
            }
            depth -= 2;
        } else if (is_operator(t, preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
                   is_operator(t, preprocessing_op_or_punc::LEFT_BRACKET) ||
                   is_operator(t, preprocessing_op_or_punc::LEFT_BRACE)) {
            unsigned end = group_end(i);
            if (end <= i + 1)
                return 0;
            i = end - 1;
        } else if (is_operator(t, preprocessing_op_or_punc::SEMICOLON) ||
                   is_operator(t,
                               preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
                   is_operator(t, preprocessing_op_or_punc::RIGHT_BRACKET) ||
                   is_operator(t, preprocessing_op_or_punc::RIGHT_BRACE)) {
            return 0;
        }
    }
}

/*
 * [temp.names]/3: "When parsing a template-argument-list, the first
 * non-nested > is taken as the ending delimiter rather than a greater-than
 * operator. Similarly, the first non-nested >> is treated as two
 * consecutive but distinct > tokens, the first of which is taken as the end
 * of the template-argument-list and completes the template-id."  The same
 * holds for the > that begins a >= or >>= (clang splits those as well).
 * Before C++11 a >> is the shift operator ([diff.cpp03.temp]/1).
 */
bool parser::at_closing_angle()
{
    if (at(preprocessing_op_or_punc::GREATER) ||
        at(preprocessing_op_or_punc::GREATER_EQUAL))
        return true;
    if (!tokens.files.behavior.has(language_feature::right_angle_brackets))
        return false;
    return at(preprocessing_op_or_punc::GREATER_GREATER) ||
           at(preprocessing_op_or_punc::GREATER_GREATER_EQUAL);
}

/*
 * Whether the '<' at the current token has a matching '>' (angle_group_end)
 * that a '(' or a '::' follows: the only template-id a speculative parse
 * after a name of unknown kind keeps.
 */
bool parser::angle_group_follows_call()
{
    unsigned end = angle_group_end(0);
    return end && (at(preprocessing_op_or_punc::LEFT_PARENTHESIS, end) ||
                   at(preprocessing_op_or_punc::COLON_COLON, end));
}

bool parser::accept_closing_angle()
{
    if (at(preprocessing_op_or_punc::GREATER))
        return accept(preprocessing_op_or_punc::GREATER);
    if (!at_closing_angle())
        return false;
    /* the first '>' of the token ends the list; the rest is the current
       token until it is consumed */
    token rest = peek();
    switch (preprocessing_op_or_punc_primary(rest.op_or_punc)) {
    case preprocessing_op_or_punc::GREATER_GREATER:
        rest.op_or_punc = preprocessing_op_or_punc::GREATER;
        break;
    case preprocessing_op_or_punc::GREATER_EQUAL:
        rest.op_or_punc = preprocessing_op_or_punc::EQUAL;
        break;
    default:
        rest.op_or_punc = preprocessing_op_or_punc::GREATER_EQUAL;
        break;
    }
    pending.append(syntax_child::token(position));
    tree.split_tokens.append(position);
    split_active = true;
    split_rest = rest;
    return true;
}

/*
 * The name since mark (an identifier, an operator-function-id or a
 * literal-operator-id), then < template-argument-list_opt >.
 */
node_index parser::template_id_rest(size_t mark, token_index first,
                                    syntax_kind kind)
{
    nesting_level level(*this);
    if (!nesting_within_limit("template-argument-lists nested no deeper "
                              "than this implementation's limit"))
        return 0;
    add_token(); /* < */
    {
        flag_set greater(greater_is_operator, false);
        if (!at_closing_angle()) {
            template_argument_list();
            if (failed())
                return 0;
        }
    }
    if (!accept_closing_angle())
        return expected("'>' to end the template-argument-list", "temp.names");
    return finish_node(kind, mark, first);
}

/*
 * One template-argument passes through, as does a list of one without its
 * ellipsis.  [temp.variadic]/5: "A pack expansion consists of a pattern
 * and an ellipsis"; a template-argument followed by ... is one (C++11).
 */
node_index parser::template_argument_list()
{
    size_t mark = begin_node();
    token_index first = position;
    unsigned count = 0;
    bool expansion = false;
    for (;;) {
        template_argument();
        if (failed())
            return 0;
        count++;
        if (at(preprocessing_op_or_punc::ELLIPSIS)) {
            if (!tokens.files.behavior.has(
                    language_feature::variadic_templates))
                return beyond_this_step();
            add_token();
            expansion = true;
        }
        if (!accept(preprocessing_op_or_punc::COMMA))
            break;
    }
    if (count == 1 && !expansion)
        return 0;
    return finish_node(syntax_kind::template_argument_list, mark, first);
}

/*
 * [temp.arg]/2: "In a template-argument, an ambiguity between a type-id
 * and an expression is resolved to a type-id, regardless of the form of
 * the corresponding template-parameter."  So a type-id that ends where the
 * template-argument can end is the argument; else it is an expression.
 * The third alternative, id-expression (a template template argument,
 * [temp.arg.template]/1), is an expression's id-expression to the parser.
 */
node_index parser::template_argument()
{
    /* template-argument: braced-init-list (CWG2450, a C++26
       production): a later edition's */
    if (at(preprocessing_op_or_punc::LEFT_BRACE))
        return beyond_this_step();
    failure_state as_type;
    {
        tentative_parse attempt(*this);
        node_index n = type_id();
        if (!failed() &&
            (at(preprocessing_op_or_punc::COMMA) ||
             at(preprocessing_op_or_punc::ELLIPSIS) || at_closing_angle())) {
            attempt.commit();
            return n;
        }
        if (!failed())
            expected("',' or '>' after the template-argument", "temp.names");
        as_type = failure;
    }
    failure_state as_expression;
    {
        tentative_parse attempt(*this);
        node_index n = constant_expression();
        if (!failed()) {
            attempt.commit();
            return n;
        }
        as_expression = failure;
    }
    if (as_type.beyond_step || as_expression.beyond_step)
        return beyond_this_step();
    if (as_type.at > as_expression.at)
        failure = as_type;
    else
        failure = as_expression;
    return 0;
}

} // namespace cplusplus
