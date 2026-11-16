/*
 * lex.separate.cpp: [lex.separate] Separate translation: source files, and
 * the positions ([cpp.line]) by which a diagnostic names a place in one.
 *
 * [lex.separate]/1: "The text of the program is kept in units called source
 * files in this document."
 */
#include <stdio.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

/* the padding after the physical characters: lookahead never checks the end */
constexpr uint32_t source_file_padding = 16;

/* the characters (already followed by the padding) become the file's */
void source_file::take(owned_array<char> text, size_t length)
{
    characters = static_cast<owned_array<char> &&>(text);
    physical_characters =
        reinterpret_cast<const unsigned char *>(characters.data());
    size = (uint32_t)length;
    index_lines();
}

bool source_file::open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    owned_array<char> text;
    size_t length = 0;
    for (;;) {
        size_t n = fread(text.extend(65536), 1, 65536, f);
        length += n;
        if (n < 65536)
            break;
    }
    fclose(f);
    if (length >= UINT32_MAX - source_file_padding)
        return false;
    /* the characters, then the padding */
    text.resize(length + source_file_padding);
    memset(text.data() + length, 0, source_file_padding);
    name = path;
    take(static_cast<owned_array<char> &&>(text), length);
    return true;
}

void source_file::from_memory(const char *file_name, const char *text,
                              size_t length)
{
    owned_array<char> copy;
    char *p = copy.extend(length + source_file_padding);
    memcpy(p, text, length);
    memset(p + length, 0, source_file_padding);
    name = file_name;
    take(static_cast<owned_array<char> &&>(copy), length);
}

/* [lex.phases]/1.1: "introducing new-line characters for end-of-line
   indicators": an end-of-line indicator is LF, CR LF or a lone CR */
void source_file::index_lines()
{
    physical_line_starts.clear();
    physical_line_starts.append(0);
    for (uint32_t i = 0; i < size; i++) {
        unsigned char c = physical_characters[i];
        if (c != '\n' && c != '\r')
            continue;
        if (c == '\r' && physical_characters[i + 1] == '\n')
            i++;
        physical_line_starts.append(i + 1);
    }
}

void source_file::add_line_directive(source_location next_line, uint32_t line,
                                     const char *presumed_name, size_t length)
{
    uint32_t lo = 0, hi = (uint32_t)physical_line_starts.size();
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        (physical_line_starts[mid] <= next_line ? lo : hi) = mid;
    }
    size_t offset = presumed_names.size();
    char *copy = presumed_names.extend(length + 1);
    memcpy(copy, presumed_name, length);
    copy[length] = 0;
    presumed_lines.append({lo, line, offset});
}

/*
 * The physical line and column of a location, or, after a line directive,
 * [cpp.line]/3: "causes the implementation to behave as if the following
 * sequence of source lines begins with a source line that has a line number
 * as specified by the digit sequence" and [cpp.line]/4 "the presumed name of
 * the source file".
 */
source_position source_file::position(source_location location) const
{
    uint32_t lo = 0, hi = (uint32_t)physical_line_starts.size();
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        (physical_line_starts[mid] <= location ? lo : hi) = mid;
    }
    source_position p = {name, lo + 1, location - physical_line_starts[lo] + 1};
    for (size_t i = presumed_lines.size(); i-- > 0;)
        if (presumed_lines[i].physical_line <= lo) {
            p.line = presumed_lines[i].presumed_line +
                     (lo - presumed_lines[i].physical_line);
            p.file_name = presumed_names.data() + presumed_lines[i].name_offset;
            break;
        }
    return p;
}

} // namespace cplusplus
