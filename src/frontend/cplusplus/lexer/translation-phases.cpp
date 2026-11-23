/*
 * translation-phases.cpp: [lex.phases] Phases of translation 1 and 2.
 *
 * [lex.phases]/1: "Implementations must behave as if these separate phases
 * occur, although in practice different phases might be folded together."
 * Phases 1 and 2 are folded into phase 3 here: the lexer never builds the
 * logical source lines, it reads them through peek() and read(), which apply
 * phases 1 and 2 to the physical characters at a position.  Most physical
 * characters are the translation character they stand for; only a backslash
 * (a splice may follow) and a '?' (with trigraphs, a trigraph may start) take
 * the longer path.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/*
 * [lex.phases]/1.1 maps physical source file characters "in an
 * implementation-defined manner": up to C++14 the nine trigraphs of
 * [diff.cpp14.lex]/1 are part of that mapping.
 */
unsigned char trigraph_replacement(unsigned char third)
{
    switch (third) {
    case '=':
        return '#';
    case '(':
        return '[';
    case '/':
        return '\\';
    case ')':
        return ']';
    case '\'':
        return '^';
    case '<':
        return '{';
    case '!':
        return '|';
    case '>':
        return '}';
    case '-':
        return '~';
    default:
        return 0;
    }
}

/* the length of an end-of-line indicator at p (LF, CR LF, CR), 0 if none */
uint32_t end_of_line_length(const unsigned char *p)
{
    if (p[0] == '\n')
        return 1;
    if (p[0] == '\r')
        return 1 + (p[1] == '\n');
    return 0;
}

} // namespace

/*
 * The translation character at physical position `at`: phase 1 (trigraphs)
 * and phase 2 (splices) applied.  With `issue` the diagnostics of what was
 * read are issued, so the lexer calls it with false while it looks ahead and
 * with true once for each character it consumes.
 */
static lexer::translation_character translate(const lexer &l, uint32_t at,
                                              bool issue)
{
    const unsigned char *p = l.file.physical_characters;
    uint32_t start = at;

    for (;;) {
        if (at >= l.file.size)
            return {0, at - start};
        unsigned char c = p[at];
        uint32_t length = 1;

        if (c == '?' && p[at + 1] == '?' && trigraph_replacement(p[at + 2])) {
            if (!l.behavior.trigraphs) {
                if (issue)
                    l.diagnostics.issue(diagnostic_id::trigraph_ignored, at);
                return {'?', at + 1 - start};
            }
            c = trigraph_replacement(p[at + 2]);
            length = 3;
            if (issue && c != '\\')
                l.diagnostics.issue(diagnostic_id::trigraph_converted, at)
                    .text(reinterpret_cast<const char *>(&c), 1);
        }
        if (c != '\\')
            return {c, at + length - start};

        /*
         * [lex.phases]/1.2: "Each instance of a backslash character (\)
         * immediately followed by a new-line character is deleted, splicing
         * physical source lines to form logical source lines."  As clang
         * (and GCC), white space between the backslash and the new-line is
         * allowed, with a warning.
         */
        uint32_t after = at + length;
        while (p[after] == ' ' || p[after] == '\t' || p[after] == '\v' ||
               p[after] == '\f')
            after++;
        uint32_t eol = 0;
        if (after < l.file.size)
            eol = end_of_line_length(p + after);
        if (!eol) {
            if (issue && length == 3)
                l.diagnostics.issue(diagnostic_id::trigraph_converted, at)
                    .text("\\", 1);
            return {'\\', at + length - start};
        }
        if (issue && after != at + length)
            l.diagnostics.issue(diagnostic_id::backslash_newline_space,
                                at + length);
        if (issue && length == 3)
            l.diagnostics.issue(diagnostic_id::trigraph_converted, at)
                .text("\\", 1);
        at = after + eol;
    }
}

/* A trigraph that phase 1 does not replace (C++17 removed them,
   [diff.cpp14.lex]/1) still gets clang's warning where a token is formed of
   it: the caller has read a '?' at `at` as itself. */
void lexer::diagnose_ignored_trigraph(uint32_t at)
{
    const unsigned char *p = file.physical_characters;
    if (!behavior.trigraphs && p[at] == '?' && p[at + 1] == '?' &&
        trigraph_replacement(p[at + 2]))
        diagnostics.issue(diagnostic_id::trigraph_ignored, at);
}

lexer::translation_character lexer::peek(uint32_t at) const
{
    unsigned char c = file.physical_characters[at];
    if (c != '\\' && c != '?' && at < file.size)
        return {c, 1};
    return translate(*this, at, false);
}

lexer::translation_character lexer::read(uint32_t at)
{
    unsigned char c = file.physical_characters[at];
    if (c != '\\' && c != '?' && at < file.size)
        return {c, 1};
    return translate(*this, at, diagnostics.suppressed == 0);
}

/*
 * The spelling of a preprocessing token: its translation characters, except
 * that [lex.pptoken]/3.1 reverts phases 1 and 2 "between the initial and
 * final double quote characters" of a raw string literal.
 */
size_t lexer::spelling(const preprocessing_token &token, vbuffer *out) const
{
    if (files)
        return files->spelling(token, out);
    return spelling_in_file(token, out);
}

size_t lexer::spelling_in_file(const preprocessing_token &token,
                               vbuffer *out) const
{
    const unsigned char *p = file.physical_characters;
    size_t before = out->n;
    if (!token.spelling_differs) {
        vbuffer_append(out, p + token.location, token.extent);
    } else {
        uint32_t end = token.location + token.extent;
        for (uint32_t at = token.location; at < end;) {
            translation_character t = peek(at);
            if (!t.extent)
                break;
            vbuffer_append(out, &t.character, 1);
            at += t.extent;
            /* the opening quote of a raw string: the rest up to the closing
               quote is physical characters */
            if (token.raw && t.character == '"') {
                /* the closing quote is the token's last physical character,
                   but phases 1 and 2 still apply to a ud-suffix after it */
                uint32_t close = end - 1;
                if (token.ud_suffix_offset)
                    close = token.location + token.ud_suffix_offset - 1;
                vbuffer_append(out, p + at, close - at + 1);
                at = close + 1;
            }
        }
    }
    return out->n - before;
}

} // namespace cplusplus
