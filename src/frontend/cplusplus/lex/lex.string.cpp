/*
 * lex.string.cpp: [lex.string] String literals and, since they end the same
 * way, [lex.ccon] Character literals, as preprocessing tokens: where each
 * ends.  Their values (phases 5 to 7) are not converted yet.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.ccon]:
 *
 *     character-literal:
 *         encoding-prefix_opt ''' c-char-sequence '''
 *
 *     c-char:
 *         any member of the basic source character set except the single-quote ''', backslash '\', or new-line character
 *         escape-sequence
 *         universal-character-name
 *
 * [lex.string]:
 *
 *     string-literal:
 *         encoding-prefix_opt '"' s-char-sequence_opt '"'
 *         encoding-prefix_opt 'R' raw-string
 *
 * For the end of the token an escape-sequence matters only in that '\'
 * escapes the next character: "\"" does not end at its second quote.  `at`
 * is the opening quote.  Without a closing quote before the end of the line,
 * [lex.pptoken]/2 makes the behavior undefined ("If a ' or a " character
 * matches the last category, the behavior is undefined"); clang warns and
 * makes the rest of the line one token of that last category, as here.
 */
uint32_t lexer::scan_character_or_string_literal(uint32_t at,
                                                 preprocessing_token &token)
{
    translation_character open = peek(at);
    unsigned char quote = open.character;
    uint32_t p = at + open.extent;
    for (;;) {
        translation_character t = peek(p);
        if (!t.extent || t.character == '\n' || t.character == '\r') {
            diagnostics.issue(diagnostic_id::ext_unterminated_char_or_string,
                              at)
                << (quote == '"' ? 1 : 0);
            token.kind = preprocessing_token_kind::other_character;
            return p;
        }
        if (t.extent != 1) {
            token.spelling_differs = true;
            read(p);
        } else if (t.character == '?')
            diagnose_ignored_trigraph(p);
        p += t.extent;
        if (t.character == quote)
            break;
        if (t.character == '\\') {
            translation_character e = peek(p);
            if (e.extent && e.character != '\n' && e.character != '\r') {
                if (e.extent != 1) {
                    token.spelling_differs = true;
                    read(p);
                }
                p += e.extent;
            }
        }
    }
    /* [lex.ccon]/7 and [lex.string] leave '' undefined; clang accepts it */
    if (quote == '\'' && p - at == 2)
        diagnostics.issue(diagnostic_id::ext_empty_character, at);
    return p;
}

/*
 * [lex.string]:
 *
 *     raw-string:
 *         '"' d-char-sequence_opt '(' r-char-sequence_opt ')' d-char-sequence_opt '"'
 *
 *     d-char:
 *         any member of the basic source character set except:
 *             space, the left parenthesis '(', the right parenthesis ')', the backslash '\', and the control characters
 *             representing horizontal tab, vertical tab, form feed, and newline.
 *
 * [lex.pptoken]/3.1: "Between the initial and final double quote characters
 * of the raw string, any transformations performed in phases 1 and 2
 * (universal-character-names and line splicing) are reverted": everything
 * after the opening quote is read as physical characters.  [lex.string]/1:
 * "A d-char-sequence shall consist of at most 16 characters."
 *
 * `quote` is the opening double quote.  Returns the end; *ok is false if
 * the delimiter is invalid, after a diagnostic.
 */
uint32_t lexer::scan_raw_string(uint32_t quote, preprocessing_token &token,
                                bool *ok)
{
    const unsigned char *p = file.physical_characters;
    uint32_t delimiter = quote + 1, length = 0;
    token.raw = true;
    diagnostics.issue(diagnostic_id::warn_cxx98_compat_raw_string_literal,
                      token.location);

    /* "d-char: any member of the basic source character set except: space,
       the left parenthesis (, the right parenthesis ), the backslash \, and
       the control characters representing horizontal tab, vertical tab, form
       feed, and newline."  C++26 adds $ @ ` (P2558R2), which clang takes in
       every edition with a warning before C++26. */
    for (;;) {
        unsigned char c = p[delimiter + length];
        bool d_char = c > ' ' && c < 0x7f && c != '(' && c != ')' && c != '\\';
        if (!d_char || length == 16)
            break;
        if (c == '$' || c == '@' || c == '`') {
            char text = (char)c;
            diagnostics
                .issue(
                    behavior.has(
                        language_feature::raw_string_delimiter_dollar_at_grave)
                        ? diagnostic_id::
                              warn_cxx26_compat_raw_string_literal_character_set
                        : diagnostic_id::
                              ext_cxx26_raw_string_literal_character_set,
                    delimiter + length)
                .text(&text, 1);
        }
        length++;
    }
    uint32_t prefix_end = delimiter + length;
    if (p[prefix_end] != '(' || prefix_end >= file.size) {
        /* [lex.string]/1: "A d-char-sequence shall consist of at most 16
           characters." */
        if (length == 16)
            diagnostics.issue(diagnostic_id::err_raw_delim_too_long,
                              prefix_end);
        else if (p[prefix_end] == '\n')
            diagnostics.issue(diagnostic_id::err_invalid_newline_raw_delim,
                              prefix_end);
        else {
            char text = (char)p[prefix_end];
            diagnostics
                .issue(diagnostic_id::err_invalid_char_raw_delim, prefix_end)
                .text(&text, 1);
        }
        /* clang's recovery: the token runs to the next double quote */
        uint32_t r = delimiter;
        while (r < file.size && p[r] != '"')
            r++;
        *ok = false;
        return r < file.size ? r + 1 : r;
    }
    /* "r-char: any member of the source character set, except a right
       parenthesis ) followed by the initial d-char-sequence (which may be
       empty) followed by a double quote ".": the first ) delimiter " ends
       the raw string */
    for (uint32_t r = prefix_end + 1; r < file.size; r++)
        if (p[r] == ')' && !memcmp(p + r + 1, p + delimiter, length) &&
            p[r + 1 + length] == '"') {
            *ok = true;
            return r + 2 + length;
        }
    /* [lex.phases]/1.3: "A source file shall not end in a partial
       preprocessing token" */
    diagnostics
        .issue(diagnostic_id::err_unterminated_raw_string, token.location)
        .text(reinterpret_cast<const char *>(p + delimiter), length);
    *ok = false;
    return file.size;
}

} // namespace cplusplus
