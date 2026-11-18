/*
 * lex.token.cpp: [lex.token] Tokens: translation phase 7's conversion of a
 * preprocessing token to a token.
 *
 *     token:
 *         identifier
 *         keyword
 *         literal
 *         operator-or-punctuator
 *
 * [lex.phases]/1.7: "Each preprocessing token is converted into a token."
 * [lex.pptoken]/1: "Each preprocessing token that is converted to a token
 * shall have the lexical form of a keyword, an identifier, a literal, or an
 * operator or punctuator."
 *
 * Phases 5 and 6 (the execution character set, concatenating string
 * literals) are not done yet: a character or string literal keeps no value.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

literal_value &lexer::add_value(token &out)
{
    literal_value &v = values.append(literal_value());
    out.value_index = (uint32_t)values.size();
    return v;
}

token lexer::convert(const preprocessing_token &pp)
{
    token out;
    out.preprocessing = pp;
    owned_buffer spelling_buffer;
    switch (pp.kind) {
    case preprocessing_token_kind::identifier: {
        spelling(pp, spelling_buffer.get());
        /* [lex.key]/1: an identifier of Table 5 is a keyword; [lex.key]/2:
           one of Table 6 is the operator it represents ([lex.digraph]/2) */
        int k = lookup_keyword(behavior, spelling_buffer.data(),
                               spelling_buffer.size());
        preprocessing_op_or_punc op;
        if (k >= 0 && keyword_is_alternative_representation(k, &op)) {
            out.kind = token_kind::operator_or_punctuator;
            out.op_or_punc = op;
        } else if (k >= 0) {
            out.kind = token_kind::keyword;
            out.keyword = (int16_t)k;
            /* [lex.bool]/1: "The Boolean literals are the keywords false and
               true."  [lex.nullptr]/1: "The pointer literal is the keyword
               nullptr." */
            if (spelling_buffer.size() == 4 &&
                !memcmp(spelling_buffer.data(), "true", 4))
                out.literal = literal_kind::boolean_literal,
                out.type = fundamental_type::bool_, add_value(out).value = 1,
                out.value_known = true;
            else if (spelling_buffer.size() == 5 &&
                     !memcmp(spelling_buffer.data(), "false", 5))
                /* its value is 0, as value_of() gives without an entry */
                out.literal = literal_kind::boolean_literal,
                out.type = fundamental_type::bool_, out.value_known = true;
            else if (spelling_buffer.size() == 7 &&
                     !memcmp(spelling_buffer.data(), "nullptr", 7))
                out.literal = literal_kind::pointer_literal,
                out.type = fundamental_type::std_nullptr_t;
        } else
            out.kind = token_kind::identifier;
        break;
    }
    case preprocessing_token_kind::pp_number:
        spelling(pp, spelling_buffer.get());
        out.kind = token_kind::literal;
        /* [lex.ppnumber]/2: the pp-number becomes an integer-literal or a
           floating-point-literal, or a user-defined-integer-literal or
           user-defined-floating-point-literal ([lex.ext]) */
        if (pp_number_analysis a; !analyze_pp_number(
                spelling_buffer.data(), spelling_buffer.size(), pp.location, a))
            out.kind = token_kind::invalid;
        else if (a.ud_suffix) {
            out.literal = literal_kind::user_defined_literal;
            out.preprocessing.ud_suffix_offset = (uint32_t)a.suffix_begin;
        } else if (a.floating)
            convert_floating_point_literal(a, out);
        else
            convert_integer_literal(a, out);
        break;
    case preprocessing_token_kind::character_literal:
        spelling(pp, spelling_buffer.get());
        out.kind = token_kind::literal;
        convert_character_literal(spelling_buffer.data(),
                                  spelling_buffer.size(), out);
        break;
    case preprocessing_token_kind::string_literal:
    case preprocessing_token_kind::user_defined_string_literal:
        /* a string literal on its own: a concatenation of one */
        return convert_string_literals(&pp, 1);
    case preprocessing_token_kind::user_defined_character_literal:
        /* [lex.ext]/6: the character-literal before the ud-suffix has its
           value as any other, which the literal operator receives */
        spelling(pp, spelling_buffer.get());
        out.kind = token_kind::literal;
        {
            size_t close = spelling_buffer.size();
            while (close > 0 && spelling_buffer.data()[close - 1] != '\'')
                close--;
            convert_character_literal(spelling_buffer.data(), close, out);
        }
        out.literal = literal_kind::user_defined_literal;
        break;
    case preprocessing_token_kind::preprocessing_op_or_punc:
        /* [lex.operators]/1: "Each operator-or-punctuator is converted to a
           single token in translation phase 7"; a preprocessing-operator
           (# ##) outside a directive has no token form */
        out.kind = token_kind::operator_or_punctuator;
        out.op_or_punc = preprocessing_op_or_punc_primary(pp.op_or_punc);
        break;
    case preprocessing_token_kind::end_of_file:
        out.kind = token_kind::end_of_file;
        break;
    default:
        /* a header-name, a placeholder of phase 4, or a single character:
           none has a token form */
        out.kind = token_kind::invalid;
        break;
    }
    return out;
}

const char *clang_token_name(const lexer &l, const token &t)
{
    switch (t.kind) {
    case token_kind::identifier:
        return "identifier";
    case token_kind::keyword:
        /* tok::getTokenName names a keyword token by its spelling: kw_int is "int" */
        return keyword_clang_name(t.keyword) + 3;
    case token_kind::operator_or_punctuator:
        /* clang names an alternative token by its primary's name, which is
           what out.op_or_punc already is */
        return preprocessing_op_or_punc_clang_name(t.op_or_punc);
    case token_kind::end_of_file:
        return "eof";
    default:
        return clang_token_name(l, t.preprocessing);
    }
}

} // namespace cplusplus
