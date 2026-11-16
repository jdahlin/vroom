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
        if (physical_line_starts[mid] <= next_line)
            lo = mid;
        else
            hi = mid;
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
        if (physical_line_starts[mid] <= location)
            lo = mid;
        else
            hi = mid;
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

namespace cplusplus
{

/* ---- [lex.separate]/1: the source files of a translation unit --------- */

namespace
{

/* while a file's lexer runs, the locations it issues are offsets in it */
struct lexing_in {
    diagnostic_consumer &diagnostics;
    source_location saved;
    lexing_in(diagnostic_consumer &d, source_location base)
        : diagnostics(d), saved(d.location_base)
    {
        diagnostics.location_base = base;
    }
    ~lexing_in()
    {
        diagnostics.location_base = saved;
    }
};

} // namespace

source_files::source_files(const implementation_defined_behavior &b,
                           diagnostic_consumer &d)
    : behavior(b), diagnostics(d), conversion(b, d, {})
{
    conversion.file.from_memory("", "", 0);
    conversion.lex.files = this;
}

void source_files::place(entry &e)
{
    e.base = next_base;
    e.index = (uint32_t)files.size() - 1;
    e.lex.files = this;
    /* one location past the end: the end of file has a place of its own */
    next_base += e.file.size + 1;
}

source_files::entry *source_files::open(const char *path)
{
    return open(path, file_options);
}

source_files::entry *source_files::open(const char *path, lexer_options o)
{
    source_file read;
    if (!read.open(path))
        return nullptr;
    entry &e = files.make(behavior, diagnostics, o);
    e.file = static_cast<source_file &&>(read);
    e.name = owned_string(path, strlen(path));
    e.file.name = e.name.c_str();
    place(e);
    return &e;
}

source_files::entry &source_files::add_text(const char *name, const char *text,
                                            size_t length, bool scratch,
                                            lexer_options o)
{
    entry &e = files.make(behavior, diagnostics, o);
    e.name = owned_string(name, strlen(name));
    e.file.from_memory(e.name.c_str(), text, length);
    e.scratch = scratch;
    place(e);
    return e;
}

preprocessing_token source_files::next(entry &e, bool header_name_permitted)
{
    lexing_in in(diagnostics, e.base);
    preprocessing_token t;
    if (header_name_permitted)
        t = e.lex.next_allowing_header_name();
    else
        t = e.lex.next();
    t.location += e.base;
    return t;
}

const source_files::entry *source_files::find(source_location location) const
{
    size_t lo = 0, hi = files.size();
    if (!hi || location < files[0].base)
        return nullptr;
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (files[mid].base <= location)
            lo = mid;
        else
            hi = mid;
    }
    return &files[lo];
}

bool source_files::is_scratch(source_location location) const
{
    const entry *e = find(location);
    return e && e->scratch;
}

source_position source_files::position(source_location location) const
{
    const entry *e = find(location);
    if (!e)
        return {"<unknown>", 0, 0};
    return e->file.position(location - e->base);
}

source_location
source_files::first_character(const preprocessing_token &token) const
{
    const entry *e = find(token.location);
    if (!e || e->scratch)
        return token.location;
    lexer::translation_character c = e->lex.peek(token.location - e->base);
    if (!c.extent)
        return token.location;
    return token.location + c.extent - 1;
}

size_t source_files::spelling(const preprocessing_token &token,
                              vbuffer *out) const
{
    const entry *e = find(token.location);
    if (!e)
        return 0;
    preprocessing_token local = token;
    local.location -= e->base;
    return e->lex.spelling_in_file(local, out);
}

token source_files::convert(const preprocessing_token &token)
{
    return conversion.lex.convert(token);
}

token source_files::convert_string_literals(const preprocessing_token *pieces,
                                            size_t count)
{
    return conversion.lex.convert_string_literals(pieces, count);
}

} // namespace cplusplus
