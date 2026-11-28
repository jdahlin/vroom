/*
 * token-stream.cpp: translation phases 5 to 7 over the preprocessor, as the
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
 * The conversions themselves are the lexer's (lexer/token.cpp,
 * lexer/string-literal.cpp); this file is their order and the buffer.  Tokens
 * are made when first asked for, so what the parser does can still reach the
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

uint8_t token_stream::layout_pragma_at(token_index i)
{
    make_through(i);
    uint8_t in_effect = 0;
    for (size_t k = 0; k < layout_change_at.size() && layout_change_at[k] <= i;
         k++)
        in_effect = layout_change_to[k];
    return in_effect;
}

uint32_t token_stream::target_pragma_at(token_index i)
{
    make_through(i);
    uint32_t in_effect = 0;
    for (size_t k = 0; k < target_change_at.size() && target_change_at[k] <= i;
         k++)
        in_effect = target_change_to[k];
    return in_effect;
}

void token_stream::resume()
{
    make_through(token_index{UINT32_MAX - 1});
    has_lookahead = false;
    if (!kept.empty() && kept.back().at.index() == tokens.size() - 1) {
        preprocessing_tokens.resize(kept.back().first);
        kept.resize(kept.size() - 1);
    }
    identifiers.resize(tokens.size() - 1);
    tokens.resize(tokens.size() - 1);
    complete = false;
}

namespace
{

bool same_piece(const preprocessing_token &a, const preprocessing_token &b)
{
    return a.kind == b.kind && a.at_start_of_line == b.at_start_of_line &&
           a.after_white_space == b.after_white_space &&
           a.spelling_differs == b.spelling_differs && a.raw == b.raw &&
           a.not_replaceable == b.not_replaceable && a.prefix == b.prefix &&
           a.op_or_punc == b.op_or_punc && a.location == b.location &&
           a.extent == b.extent && a.expansion == b.expansion &&
           a.ud_suffix_offset == b.ud_suffix_offset;
}

} // namespace

void token_stream::make_more(token_index i)
{
    while (!complete && tokens.size() <= i.index()) {
        preprocessing_token first = read();
        if (files.layout_pragma != layout_pragma_before) {
            /* a layout pragma ran before this token */
            layout_pragma_before = files.layout_pragma;
            layout_change_at.append(
                token_index{static_cast<uint32_t>(tokens.size())});
            layout_change_to.append(layout_pragma_before);
        }
        if (files.target_pragma != target_pragma_before) {
            target_pragma_before = files.target_pragma;
            target_change_at.append(
                token_index{static_cast<uint32_t>(tokens.size())});
            target_change_to.append(target_pragma_before);
        }
        size_t begin = preprocessing_tokens.size();
        preprocessing_tokens.append(first);
        identifiers.append(0);
        if (is_string_literal(first)) {
            /* [lex.phases]/1.6: the string literals that follow are part of
               the same token */
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
            size_t issued = files.diagnostics.messages.size();
            tokens.append(files.convert(first));
            token &made = tokens.back();
            /* [lex.ppnumber]/2: a pp-number that is no literal is a valid
               preprocessing token that converts to no token: an error
               only where the grammar wants a literal (the parser stops
               there), not where it takes a pp-number, as clang's
               availability takes a version 10.12.1 */
            if (made.kind == token_kind::invalid &&
                first.kind == preprocessing_token_kind::pp_number)
                files.diagnostics.discard_since(issued);
            if (made.kind == token_kind::keyword && made.keyword >= 0 &&
                static_cast<size_t>(made.keyword) <
                    revertible_keywords.size() &&
                revertible_keywords[static_cast<size_t>(made.keyword)]) {
                /* [ext:clang_type_trait_names] */
                lookahead = read();
                has_lookahead = true;
                if (!(lookahead.kind ==
                          preprocessing_token_kind::preprocessing_op_or_punc &&
                      preprocessing_op_or_punc_primary(lookahead.op_or_punc) ==
                          preprocessing_op_or_punc::LEFT_PARENTHESIS))
                    made.kind = token_kind::identifier;
            }
        }
        size_t count = preprocessing_tokens.size() - begin;
        if (count == 1 && same_piece(first, tokens.back().preprocessing))
            preprocessing_tokens.resize(begin);
        else
            kept.append({token_index{static_cast<uint32_t>(tokens.size() - 1)},
                         static_cast<uint32_t>(begin),
                         static_cast<uint32_t>(count)});
        if (first.kind == preprocessing_token_kind::end_of_file)
            complete = true;
    }
}

const token &token_stream::operator[](token_index i)
{
    make_through(i);
    if (i.index() < tokens.size())
        return tokens[i];
    return tokens.back();
}

bool token_stream::at_end(token_index i)
{
    make_through(i);
    return i.index() + 1 >= tokens.size() && complete;
}

const preprocessing_token *token_stream::pieces(token_index i, size_t *count)
{
    make_through(i);
    if (i.index() >= tokens.size())
        i = token_index{static_cast<uint32_t>(tokens.size() - 1)};
    size_t low = 0, high = kept.size();
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (kept[middle].at < i)
            low = middle + 1;
        else
            high = middle;
    }
    if (low < kept.size() && kept[low].at == i) {
        *count = kept[low].count;
        return preprocessing_tokens.data() + kept[low].first;
    }
    *count = 1;
    return &tokens[i].preprocessing;
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
    if (i.index() < identifiers.size() && t.kind != token_kind::identifier)
        return identifiers[i];
    if (t.kind != token_kind::identifier || i.index() >= tokens.size())
        return 0;
    if (!identifiers[i])
        identifiers[i] = pp.identifier(t.preprocessing) + 1;
    return identifiers[i];
}

void token_stream::name_token(token_index i, uint32_t id)
{
    if (i.index() < identifiers.size())
        identifiers[i] = id;
}

} // namespace cplusplus
