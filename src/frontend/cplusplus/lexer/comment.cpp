/*
 * comment.cpp: [lex.comment] Comments, and the white space between
 * preprocessing tokens ([lex.pptoken]/2).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.pptoken]/2: white space "consists of comments, or white-space
 * characters (space, horizontal tab, new-line, vertical tab, and form-feed),
 * or both".  The white-space characters from `at`; comments are scanned by
 * scan_comment.  A splice between them is part of the white space.
 */
uint32_t lexer::scan_white_space(uint32_t at)
{
    for (;;) {
        translation_character t = peek(at);
        if (!t.extent || !is_white_space_character(t.character))
            return at;
        at += t.extent;
    }
}

/*
 * [lex.comment]/1: "The characters /\* start a comment, which terminates with
 * the characters *\/.  These comments do not nest.  The characters // start a
 * comment, which terminates immediately before the next new-line character."
 * (A backslash keeps the quoted characters from ending this comment.)
 *
 * `at` is the '/'; the caller has seen the second character.  Returns the end
 * of the comment (before the new-line of a // comment).
 */
uint32_t lexer::scan_comment(uint32_t at)
{
    const unsigned char *p = file.physical_characters;
    translation_character slash = peek(at);
    translation_character second = peek(at + slash.extent);
    uint32_t start = at;
    at += slash.extent + second.extent;

    if (second.character == '/') {
        for (;;) {
            unsigned char c = p[at];
            if (at >= file.size || c == '\n' || c == '\r')
                return at;
            if (c != '\\' && c != '?') {
                at++;
                continue;
            }
            translation_character t = peek(at);
            if (!t.extent || t.character == '\n' || t.character == '\r')
                return at;
            /* a splice continues the comment on the next physical line */
            if (t.extent > 1 && p[at] == '\\')
                diagnostics.issue(diagnostic_id::ext_multi_line_line_comment,
                                  start);
            at += t.extent;
        }
    }

    /* a block comment: the first star-slash after the opening ends it */
    bool after_star = false;
    for (;;) {
        if (at >= file.size) {
            /* [lex.phases]/1.3: "A source file shall not end in a partial
               preprocessing token or in a partial comment." */
            diagnostics.issue(diagnostic_id::err_unterminated_block_comment,
                              start);
            return at;
        }
        unsigned char c = p[at];
        if (c != '*' && c != '/' && c != '\\' && c != '?') {
            after_star = false;
            at++;
            continue;
        }
        translation_character t = peek(at);
        if (t.character == '/' && after_star)
            return at + t.extent;
        if (t.character == '/' && peek(at + t.extent).character == '*' &&
            peek(at + t.extent + peek(at + t.extent).extent).character != '/')
            /* "These comments do not nest": clang warns about what looks
               like an attempt */
            diagnostics.issue(diagnostic_id::warn_nested_block_comment, at);
        after_star = t.character == '*';
        if (t.extent)
            at += t.extent;
        else
            at += 1;
    }
}

} // namespace cplusplus
