/*
 * over.oper.cpp: [over.oper] Overloaded operators and [over.literal]
 * User-defined literals: the unqualified-ids that begin with operator
 * ([expr.prim.id.unqual]), with [class.conv.fct]'s conversion-function-id
 * (class.conv.fct.cpp).
 *
 *     operator-function-id:
 *         operator operator
 *
 *     operator: one of
 *         new delete new[] delete[] co_await () [] -> ->*
 *         ~ ! + - * / % ^ &
 *         | = += -= *= /= %= ^= &=
 *         |= == != < > <= >= <=> &&
 *         || << >> <<= >>= ++ -- ,
 *
 *     literal-operator-id:
 *         operator string-literal identifier
 *         operator user-defined-string-literal
 *
 * [over.oper]/1: "Both the unary and binary forms of + - * & can be
 * overloaded"; "The following operators cannot be overloaded: . .* :: ?:".
 * A template-id of an operator-function-id or literal-operator-id is P7's.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* [over.oper]/1's operator, the tokens that are one */
bool is_overloadable_operator(const token &t)
{
    if (t.kind != token_kind::operator_or_punctuator)
        return false;
    switch (preprocessing_op_or_punc_primary(t.op_or_punc)) {
    case preprocessing_op_or_punc::ARROW:
    case preprocessing_op_or_punc::ARROW_STAR:
    case preprocessing_op_or_punc::TILDE:
    case preprocessing_op_or_punc::EXCLAMATION:
    case preprocessing_op_or_punc::PLUS:
    case preprocessing_op_or_punc::MINUS:
    case preprocessing_op_or_punc::STAR:
    case preprocessing_op_or_punc::SLASH:
    case preprocessing_op_or_punc::PERCENT:
    case preprocessing_op_or_punc::CARET:
    case preprocessing_op_or_punc::AMPERSAND:
    case preprocessing_op_or_punc::BAR:
    case preprocessing_op_or_punc::EQUAL:
    case preprocessing_op_or_punc::PLUS_EQUAL:
    case preprocessing_op_or_punc::MINUS_EQUAL:
    case preprocessing_op_or_punc::STAR_EQUAL:
    case preprocessing_op_or_punc::SLASH_EQUAL:
    case preprocessing_op_or_punc::PERCENT_EQUAL:
    case preprocessing_op_or_punc::CARET_EQUAL:
    case preprocessing_op_or_punc::AMPERSAND_EQUAL:
    case preprocessing_op_or_punc::BAR_EQUAL:
    case preprocessing_op_or_punc::EQUAL_EQUAL:
    case preprocessing_op_or_punc::EXCLAMATION_EQUAL:
    case preprocessing_op_or_punc::LESS:
    case preprocessing_op_or_punc::GREATER:
    case preprocessing_op_or_punc::LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_EQUAL:
    case preprocessing_op_or_punc::SPACESHIP:
    case preprocessing_op_or_punc::AMPERSAND_AMPERSAND:
    case preprocessing_op_or_punc::BAR_BAR:
    case preprocessing_op_or_punc::LESS_LESS:
    case preprocessing_op_or_punc::GREATER_GREATER:
    case preprocessing_op_or_punc::LESS_LESS_EQUAL:
    case preprocessing_op_or_punc::GREATER_GREATER_EQUAL:
    case preprocessing_op_or_punc::PLUS_PLUS:
    case preprocessing_op_or_punc::MINUS_MINUS:
    case preprocessing_op_or_punc::COMMA:
        return true;
    default:
        return false;
    }
}

} // namespace

/*
 * The unqualified-id at operator: which of the three it is follows from the
 * token after operator.  [over.literal]/1: "The string-literal or
 * user-defined-string-literal in a literal-operator-id shall have no
 * encoding-prefix and shall contain no characters other than the implicit
 * terminating '\0'."  User-defined literals are C++11's (N2765).
 */
node_index parser::operator_unqualified_id()
{
    size_t mark = begin_node();
    token_index first = position;
    add_token(); /* operator */
    const token &t = peek();
    if (t.literal == literal_kind::string_literal ||
        (t.literal == literal_kind::user_defined_literal &&
         t.preprocessing.kind ==
             preprocessing_token_kind::user_defined_string_literal)) {
        if (!tokens.files.behavior.has(language_feature::user_defined_literals))
            return beyond_this_step();
        /* [lex.phases]/1.6: the token is the concatenation of adjacent
           string-literals; each of them has to be empty and without
           encoding-prefix (R is none, [lex.string]/1), the last may be the
           user-defined one */
        bool suffixed = t.literal == literal_kind::user_defined_literal;
        size_t count = 0;
        const preprocessing_token *pieces = tokens.pieces(position, &count);
        for (size_t i = 0; i < count; i++) {
            owned_buffer spelling;
            tokens.files.spelling(pieces[i], spelling.get());
            const char *text = spelling.data();
            bool prefixed =
                spelling.size() < 2 ||
                (text[0] != '"' && !(text[0] == 'R' && text[1] == '"'));
            /* empty: "" or R"d()d", whatever ud-suffix follows */
            size_t open = 1;
            if (!prefixed && text[0] == 'R')
                while (open < spelling.size() && text[open] != '(')
                    open++;
            bool empty = !prefixed;
            if (empty && text[0] == 'R')
                empty = open + 1 < spelling.size() && text[open + 1] == ')';
            else if (empty)
                empty = text[1] == '"';
            if (!empty)
                return violates("an empty string-literal with no "
                                "encoding-prefix after operator",
                                "over.literal", position);
        }
        add_token();
        if (!suffixed) {
            if (!at_identifier())
                return expected("an identifier after the string-literal",
                                "over.literal");
            add_token();
        }
        node_index n =
            finish_node(syntax_kind::literal_operator_id, mark, first);
        /* template-id: literal-operator-id < template-argument-list_opt > */
        if (at(preprocessing_op_or_punc::LESS))
            return template_id_rest(mark, first, syntax_kind::template_id);
        return n;
    }
    if (at(standard_keyword::new_) || at(standard_keyword::delete_)) {
        add_token();
        if (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
            at(preprocessing_op_or_punc::RIGHT_BRACKET, 1)) {
            add_token();
            add_token();
        }
    } else if (at(standard_keyword::co_await_)) {
        add_token();
    } else if ((at(preprocessing_op_or_punc::LEFT_PARENTHESIS) &&
                at(preprocessing_op_or_punc::RIGHT_PARENTHESIS, 1)) ||
               (at(preprocessing_op_or_punc::LEFT_BRACKET) &&
                at(preprocessing_op_or_punc::RIGHT_BRACKET, 1))) {
        add_token();
        add_token();
    } else if (is_overloadable_operator(t)) {
        add_token();
    } else {
        /* conversion-function-id: operator conversion-type-id */
        conversion_type_id();
        if (failed())
            return 0;
        return finish_node(syntax_kind::conversion_function_id, mark, first);
    }
    node_index n = finish_node(syntax_kind::operator_function_id, mark, first);
    /* template-id: operator-function-id < template-argument-list_opt > */
    if (at(preprocessing_op_or_punc::LESS))
        return template_id_rest(mark, first, syntax_kind::template_id);
    return n;
}

} // namespace cplusplus
