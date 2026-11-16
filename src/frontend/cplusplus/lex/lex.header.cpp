/*
 * lex.header.cpp: [lex.header] Header names.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/*
 * [lex.header]:
 *
 *     header-name:
 *         '<' h-char-sequence '>'
 *         '"' q-char-sequence '"'
 *
 *     h-char:
 *         any member of the source character set except new-line and '>'
 *
 *     q-char:
 *         any member of the source character set except new-line and '"'
 *
 * Only where [lex.pptoken]/3.3 lets one be formed.  [lex.header]/2 makes
 * ' \ /\* // in a header-name conditionally-supported: clang takes them as
 * they are, as here.
 */
uint32_t lexer::scan_header_name(uint32_t at, bool *ok)
{
    translation_character open = peek(at);
    unsigned char close = '"';
    if (open.character == '<')
        close = '>';
    uint32_t p = at + open.extent;
    for (;;) {
        translation_character t = peek(p);
        if (!t.extent || t.character == '\n' || t.character == '\r') {
            *ok = false;
            return at;
        }
        p += t.extent;
        if (t.character == close) {
            *ok = true;
            return p;
        }
    }
}

} // namespace cplusplus
