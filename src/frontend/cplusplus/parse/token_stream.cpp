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
        if (is_string_literal(first)) {
            /* [lex.phases]/1.6: the string literals that follow are part of
               the same token */
            size_t begin = string_pieces.size();
            string_first.append(static_cast<uint32_t>(begin));
            string_pieces.append(first);
            for (;;) {
                preprocessing_token next = read();
                if (!is_string_literal(next)) {
                    lookahead = next;
                    has_lookahead = true;
                    break;
                }
                string_pieces.append(next);
            }
            token &made = tokens.append(files.convert_string_literals(
                string_pieces.data() + begin, string_pieces.size() - begin));
            made.pieces = static_cast<uint32_t>(string_first.size());
        } else if (first.kind == preprocessing_token_kind::identifier &&
                   identifier_converted(first)) {
            /* converted */
        } else {
            tokens.append(files.convert(first));
        }
        if (!is_string_literal(first)) {
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

/*
 * OPTIMIZATION: phase 7 of an identifier through the preprocessor's
 * identifier table: interned once (the parser's identifier() then has it),
 * its keyword looked up once per identifier and translation unit.  A
 * spelling with a universal-character-name is interned as the character it
 * names, which no keyword is spelled with: lexer::convert decides that one.
 */
bool token_stream::identifier_converted(const preprocessing_token &first)
{
    uint32_t id;
    if (first.identifier) {
        id = first.identifier - 1;
    } else {
        id = pp.identifier(first);
        if (memchr(pp.spelling_buffer.data(), '\\', pp.spelling_buffer.size()))
            return false;
    }
    while (keyword_by_identifier.size() <= id)
        keyword_by_identifier.append(unknown_keyword);
    size_t length;
    const char *spelling = pp.identifier_spelling(id, &length);
    if (keyword_by_identifier[id] == unknown_keyword)
        keyword_by_identifier[id] = static_cast<int16_t>(
            lookup_keyword(files.behavior, spelling, length));
    token made;
    made.preprocessing = first;
    made.preprocessing.identifier = id + 1;
    identifier_token(made, nullptr, keyword_by_identifier[id], spelling,
                     length);
    tokens.append(made);
    return true;
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
    uint32_t k = tokens[i].pieces;
    if (!k) {
        *count = 1;
        return &tokens[i].preprocessing;
    }
    size_t end = string_pieces.size();
    if (k < string_first.size())
        end = string_first[k];
    *count = end - string_first[k - 1];
    return string_pieces.data() + string_first[k - 1];
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
    /* the identifier's index, kept in the token once known */
    token &kept = tokens[i];
    if (!kept.preprocessing.identifier)
        kept.preprocessing.identifier = pp.identifier(kept.preprocessing) + 1;
    return kept.preprocessing.identifier;
}

} // namespace cplusplus
