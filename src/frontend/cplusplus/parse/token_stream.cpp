/*
 * token_stream.cpp: translation phases 5 to 7 over the preprocessor, as the
 * indexed sequence of tokens the parser reads.
 *
 * [lex.phases]/1.5: "Each basic source character set member in a
 * character-literal or a string-literal, as well as each escape sequence and
 * universal-character-name in a character-literal or a non-raw string
 * literal, is converted to the corresponding member of the execution
 * character set".
 * [lex.phases]/1.6: "Adjacent string literal tokens are concatenated."
 * [lex.phases]/1.7: "White-space characters separating tokens are no longer
 * significant.  Each preprocessing token is converted into a token."
 *
 * The conversions themselves are the lexer's (lex/lex.token.cpp,
 * lex/lex.string.cpp); this file is their order and the buffer.  Tokens are
 * made when first asked for, so what the parser does can still reach the
 * preprocessor before a later token is read.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

token_stream::token_stream(preprocessor &pp) : pp(pp), files(pp.files)
{
}

preprocessing_token token_stream::read()
{
    if (has_lookahead) {
        has_lookahead = false;
        return lookahead;
    }
    return pp.next();
}

void token_stream::make_through(token_index i)
{
    while (!complete && tokens.size() <= i) {
        preprocessing_token first = read();
        first_piece.append(static_cast<uint32_t>(preprocessing_tokens.size()));
        preprocessing_tokens.append(first);
        identifiers.append(0);
        if (is_string_literal(first)) {
            /* [lex.phases]/1.6: the string literals that follow are part of
               the same token */
            size_t begin = preprocessing_tokens.size() - 1;
            for (;;) {
                preprocessing_token next = read();
                if (!is_string_literal(next)) {
                    lookahead = next;
                    has_lookahead = true;
                    break;
                }
                preprocessing_tokens.append(next);
            }
            tokens.append(files.convert_string_literals(
                preprocessing_tokens.data() + begin,
                preprocessing_tokens.size() - begin));
        } else {
            tokens.append(files.convert(first));
            token &made = tokens.back();
            if (made.kind == token_kind::keyword && made.keyword >= 0 &&
                static_cast<size_t>(made.keyword) <
                    revertible_keywords.size() &&
                revertible_keywords[made.keyword]) {
                /* [ext.clang] clang_type_trait_names */
                lookahead = read();
                has_lookahead = true;
                if (!(lookahead.kind ==
                          preprocessing_token_kind::preprocessing_op_or_punc &&
                      preprocessing_op_or_punc_primary(lookahead.op_or_punc) ==
                          preprocessing_op_or_punc::LEFT_PARENTHESIS))
                    made.kind = token_kind::identifier;
            }
        }
        if (first.kind == preprocessing_token_kind::end_of_file)
            complete = true;
    }
}

const token &token_stream::operator[](token_index i)
{
    make_through(i);
    if (i < tokens.size())
        return tokens[i];
    return tokens.back();
}

bool token_stream::at_end(token_index i)
{
    make_through(i);
    return i + 1 >= tokens.size() && complete;
}

const preprocessing_token *token_stream::pieces(token_index i, size_t *count)
{
    make_through(i);
    if (i >= tokens.size())
        i = static_cast<token_index>(tokens.size() - 1);
    size_t end = preprocessing_tokens.size();
    if (i + 1 < tokens.size())
        end = first_piece[i + 1];
    *count = end - first_piece[i];
    return preprocessing_tokens.data() + first_piece[i];
}

source_location token_stream::location(token_index i)
{
    const preprocessing_token &p = (*this)[i].preprocessing;
    if (p.expansion)
        return p.expansion;
    return p.location;
}

source_position token_stream::position(token_index i)
{
    return files.position(location(i));
}

size_t token_stream::spelling(token_index i, vbuffer *out)
{
    size_t count, written = 0;
    const preprocessing_token *piece = pieces(i, &count);
    for (size_t k = 0; k < count; k++) {
        if (piece[k].kind == preprocessing_token_kind::end_of_file)
            break;
        if (k) {
            vbuffer_append(out, " ", 1);
            written++;
        }
        written += files.spelling(piece[k], out);
    }
    return written;
}

uint32_t token_stream::identifier(token_index i)
{
    const token &t = (*this)[i];
    if (t.kind != token_kind::identifier || i >= tokens.size())
        return 0;
    if (!identifiers[i])
        identifiers[i] = pp.identifier(t.preprocessing) + 1;
    return identifiers[i];
}

} // namespace cplusplus
