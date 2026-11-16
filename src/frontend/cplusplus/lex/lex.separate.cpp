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

bool source_file::open(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return false;
    size_t capacity = 1 << 16, length = 0, n;
    char *text = static_cast<char *>(malloc(capacity + source_file_padding));
    while (text && (n = fread(text + length, 1, capacity - length, f)) > 0) {
        length += n;
        if (length == capacity)
            text = static_cast<char *>(
                realloc(text, (capacity *= 2) + source_file_padding));
    }
    fclose(f);
    if (!text || length >= UINT32_MAX - source_file_padding) {
        free(text);
        return false;
    }
    memset(text + length, 0, source_file_padding);
    owned = text;
    name = path;
    physical_characters = reinterpret_cast<const unsigned char *>(text);
    size = (uint32_t)length;
    index_lines();
    return true;
}

void source_file::from_memory(const char *file_name, const char *text,
                              size_t length)
{
    owned = static_cast<char *>(malloc(length + source_file_padding));
    if (!owned)
        abort();
    memcpy(owned, text, length);
    memset(owned + length, 0, source_file_padding);
    name = file_name;
    physical_characters = reinterpret_cast<const unsigned char *>(owned);
    size = (uint32_t)length;
    index_lines();
}

void source_file::close()
{
    free(owned);
    free(physical_line_starts);
    free(presumed_lines);
    *this = source_file{};
}

/* [lex.phases]/1.1: "introducing new-line characters for end-of-line
   indicators": an end-of-line indicator is LF, CR LF or a lone CR */
void source_file::index_lines()
{
    uint32_t capacity = 64;
    physical_line_starts =
        static_cast<uint32_t *>(malloc(capacity * sizeof(uint32_t)));
    physical_line_starts[0] = 0;
    physical_line_count = 1;
    for (uint32_t i = 0; i < size; i++) {
        unsigned char c = physical_characters[i];
        if (c != '\n' && c != '\r')
            continue;
        if (c == '\r' && physical_characters[i + 1] == '\n')
            i++;
        if (physical_line_count == capacity)
            physical_line_starts = static_cast<uint32_t *>(realloc(
                physical_line_starts, (capacity *= 2) * sizeof(uint32_t)));
        physical_line_starts[physical_line_count++] = i + 1;
    }
}

void source_file::add_line_directive(source_location next_line, uint32_t line,
                                     const char *presumed_name)
{
    uint32_t lo = 0, hi = physical_line_count;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        (physical_line_starts[mid] <= next_line ? lo : hi) = mid;
    }
    presumed_lines = static_cast<presumed_line *>(realloc(
        presumed_lines, (presumed_line_count + 1) * sizeof(presumed_line)));
    presumed_lines[presumed_line_count++] = {lo, line, presumed_name};
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
    uint32_t lo = 0, hi = physical_line_count;
    while (hi - lo > 1) {
        uint32_t mid = (lo + hi) / 2;
        (physical_line_starts[mid] <= location ? lo : hi) = mid;
    }
    source_position p = {name, lo + 1, location - physical_line_starts[lo] + 1};
    for (uint32_t i = presumed_line_count; i-- > 0;)
        if (presumed_lines[i].physical_line <= lo) {
            p.line = presumed_lines[i].presumed_line +
                     (lo - presumed_lines[i].physical_line);
            p.file_name = presumed_lines[i].presumed_name;
            break;
        }
    return p;
}

} // namespace cplusplus
