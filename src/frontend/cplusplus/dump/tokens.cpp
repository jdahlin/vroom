/*
 * tokens.cpp: the developer outputs of phases 1 to 7 (dump.h):
 * raw-tokens, definitions, preprocessed, tokens, literal-claims and
 * parser-tokens, which print what clang -cc1 prints for the same file so
 * that bench/cxx-lex.py, cxx-pp.py and cxx-parse.py can compare the two.
 */
#include <string.h>

#include "../cplusplus-internal.h"
#include "dump.h"

namespace cplusplus
{

static void dump_escaped(vbuffer *out, const char *s, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\\' || c == '"')
            vbuffer_format_append(out, "\\%c", c);
        else if (c == '\n')
            vbuffer_string_append(out, "\\n");
        else if (c == '\t')
            vbuffer_string_append(out, "\\t");
        else if (c < 0x20 || c >= 0x7f)
            vbuffer_format_append(out, "\\%03o", c);
        else
            vbuffer_append(out, &c, 1);
    }
}

void dump_token(vbuffer *out, const char *name, const char *spelling, size_t n,
                source_position p, const source_files *files,
                const preprocessing_token &pp, bool raw,
                const unsigned char *physical)
{
    vbuffer_format_append(out, "%s '", name);
    dump_escaped(out, spelling, n);
    vbuffer_format_append(out, "'\tLoc=<%s:%u:%u", p.file_name, p.line,
                          p.column);
    if (files && pp.expansion) {
        source_position s = files->position(pp.location);
        vbuffer_format_append(out, " <Spelling=%s:%u:%u>", s.file_name, s.line,
                              s.column);
    }
    vbuffer_string_append(out, ">");
    if (pp.at_start_of_line)
        vbuffer_string_append(out, " [StartOfLine]");
    if (pp.after_white_space && !raw)
        vbuffer_string_append(out, " [LeadingSpace]");
    if (pp.spelling_differs && physical) {
        vbuffer_string_append(out, " [UnClean='");
        dump_escaped(out, reinterpret_cast<const char *>(physical), pp.extent);
        vbuffer_string_append(out, "']");
    }
    vbuffer_string_append(out, "\n");
}

/*
 * A literal's type and value as static_asserts clang can check:
 *   _Static_assert(__is_same(__decltype(LIT), T), "");
 *   _Static_assert(LIT == (T)V, "");       integer and character literals
 *   _Static_assert(LIT[k] == (T)V_k, "");  each code unit of a string literal
 * with #line so that a failed claim names the literal's place (clang's
 * spellings, which every edition has).
 */
static void dump_literal_claim(vbuffer *out, const lexer &l, const token &t,
                               const char *spelling, size_t n,
                               source_position p)
{
    if (t.kind != token_kind::literal || !t.value_known ||
        t.literal == literal_kind::user_defined_literal ||
        t.type == fundamental_type::bit_precise_int ||
        t.type == fundamental_type::unsigned_bit_precise_int)
        return;
    const char *type = fundamental_type_name(t.type);
    vbuffer_format_append(out, "#line %u \"%s\"\n", p.line, p.file_name);
    if (t.literal == literal_kind::floating_point_literal) {
        /* the bits of the value: __builtin_bit_cast to an unsigned integer
           of the format's width */
        const char *bits = "unsigned __int128";
        if (t.type == fundamental_type::float16)
            bits = "unsigned short";
        else if (t.type == fundamental_type::float_)
            bits = "unsigned int";
        else if (t.type == fundamental_type::double_)
            bits = "unsigned long long";
        vbuffer_format_append(
            out,
            "_Static_assert(__is_same(__decltype(%.*s), %s), \"\"); "
            "_Static_assert(__builtin_bit_cast(%s, %.*s) == "
            "(((unsigned __int128)0x%llxull << 64) | 0x%llxull), \"\");\n",
            (int)n, spelling, type, bits, (int)n, spelling,
            (unsigned long long)t.floating_bits_high,
            (unsigned long long)t.floating_bits_low);
        return;
    }
    if (t.literal == literal_kind::string_literal) {
        vbuffer_format_append(out,
                              "_Static_assert(__is_same(__decltype(%.*s), "
                              "const %s(&)[%u]), \"\");",
                              (int)n, spelling, type, t.string_length + 1);
        for (uint32_t k = 0; k < t.string_length && k < 64; k++)
            vbuffer_format_append(
                out, " _Static_assert(%.*s[%u] == (%s)0x%xu, \"\");", (int)n,
                spelling, k, type, l.string_code_units()[t.string_offset + k]);
        vbuffer_string_append(out, "\n");
        return;
    }
    vbuffer_format_append(
        out,
        "_Static_assert(__is_same(__decltype(%.*s), %s), \"\"); "
        "_Static_assert(%.*s == (%s)0x%llxull, \"\");\n",
        (int)n, spelling, type, (int)n, spelling, type,
        (unsigned long long)t.value);
}

static bool dump_is_string(const preprocessing_token &pp)
{
    return pp.kind == preprocessing_token_kind::string_literal ||
           pp.kind == preprocessing_token_kind::user_defined_string_literal;
}

bool dump_raw_tokens(const char *path,
                     const implementation_defined_behavior &behavior,
                     diagnostic_consumer &diagnostics, vbuffer *out,
                     vbuffer *messages)
{
    source_file file;
    if (!file.open(path))
        return false;
    diagnostics.file = &file;
    lexer_options options;
    options.retain_white_space = options.retain_comments = true;
    lexer l(file, behavior, diagnostics, options);
    owned_buffer spelling;
    for (preprocessing_token pp = l.next();
         pp.kind != preprocessing_token_kind::end_of_file; pp = l.next()) {
        spelling.clear();
        l.spelling(pp, spelling.get());
        dump_token(out, clang_token_name(l, pp), spelling.data(),
                   spelling.size(), file.position(pp.location), nullptr, pp,
                   true, file.physical_characters + pp.location);
    }
    /* the diagnostics are rendered while the file is open */
    for (const diagnostic_message &m : diagnostics.messages)
        diagnostics.render(m, messages);
    diagnostics.file = nullptr;
    return true;
}

/* #embed's bytes ([ext.clang]): a scratch file named <embed> */
static bool dump_embedded(const source_files &files,
                          const preprocessing_token &t)
{
    const source_files::entry *e = files.find(t.location);
    return e && e->scratch && !strcmp(e->name.c_str(), "<embed>");
}

void dump_preprocessing(const dump_request &r, source_files &files,
                        preprocessor &pp, diagnostic_consumer &diagnostics,
                        vbuffer *out)
{
    owned_array<preprocessing_token> tokens;
    for (;;) {
        preprocessing_token t = pp.next();
        tokens.append(t);
        if (t.kind == preprocessing_token_kind::end_of_file)
            break;
    }
    if (r.has(dump_output::definitions))
        pp.write_definitions(out);
    if (r.has(dump_output::preprocessed)) {
        /* the preprocessing tokens, a new line where a line started, a
           space where white space was (clang -E -P without its spacing
           heuristics) */
        bool first = true;
        for (const preprocessing_token &t : tokens) {
            if (t.kind == preprocessing_token_kind::end_of_file)
                break;
            if (!first && t.at_start_of_line)
                vbuffer_append(out, "\n", 1);
            else if (!first && t.after_white_space)
                vbuffer_append(out, " ", 1);
            first = false;
            files.spelling(t, out);
        }
        if (!first)
            vbuffer_append(out, "\n", 1);
    }
    bool dump_tokens = r.has(dump_output::tokens);
    bool claims = r.has(dump_output::literal_claims);
    if (!dump_tokens && !claims)
        return;

    /* phases 5 to 7 */
    const lexer &l = files.phase7();
    if (r.phase4_diagnostics_only)
        diagnostics.suppressed++;
    owned_buffer spelling;
    for (size_t i = 0; i < tokens.size(); i++) {
        const preprocessing_token &p = tokens[i];
        /* #embed's bytes: one line, as clang's annot_embed */
        if (dump_tokens && dump_embedded(files, p)) {
            while (i + 1 < tokens.size() && dump_embedded(files, tokens[i + 1]))
                i++;
            vbuffer_string_append(out, "annot_embed ''\tLoc=<embed>\n");
            continue;
        }
        /* [lex.phases]/1.6: adjacent string literals become one token; each
           piece is still printed as clang prints it */
        size_t pieces = 1;
        token t;
        if (dump_is_string(p)) {
            while (i + pieces < tokens.size() &&
                   dump_is_string(tokens[i + pieces]))
                pieces++;
            t = files.convert_string_literals(&tokens[i], pieces);
        } else
            t = files.convert(p);
        if (claims) {
            if (p.kind != preprocessing_token_kind::end_of_file) {
                /* the claim is about the concatenation, spelled as its
                   pieces */
                spelling.clear();
                for (size_t k = 0; k < pieces; k++) {
                    if (k)
                        vbuffer_append(spelling.get(), " ", 1);
                    files.spelling(tokens[i + k], spelling.get());
                }
                dump_literal_claim(out, l, t, spelling.data(), spelling.size(),
                                   files.position(p.location));
            }
            i += pieces - 1;
            continue;
        }
        for (size_t k = 0; k < pieces; k++) {
            const preprocessing_token &piece = tokens[i + k];
            const char *name = clang_token_name(l, t);
            if (pieces > 1)
                name = clang_token_name(l, piece);
            spelling.clear();
            if (piece.kind != preprocessing_token_kind::end_of_file)
                files.spelling(piece, spelling.get());
            const source_files::entry *e = files.find(piece.location);
            /* a replaced token is where its invocation is; its physical
               characters only when it is not */
            source_location place = piece.location;
            const unsigned char *physical = nullptr;
            if (piece.expansion)
                place = piece.expansion;
            else if (e)
                physical =
                    e->file.physical_characters + piece.location - e->base;
            dump_token(out, name, spelling.data(), spelling.size(),
                       files.position(place), &files, piece, false, physical);
        }
        i += pieces - 1;
    }
}

void dump_parser_tokens(const dump_request &r, source_files &files,
                        token_stream &tokens, diagnostic_consumer &diagnostics,
                        vbuffer *out)
{
    /* the same lines from the same tokens as tokens: how bench/cxx-parse.py
       knows the token stream adds and loses nothing */
    const lexer &l = files.phase7();
    if (r.phase4_diagnostics_only)
        diagnostics.suppressed++;
    owned_buffer spelling;
    for (token_index i = 0;; i++) {
        const token t = tokens[i];
        size_t count;
        const preprocessing_token *piece = tokens.pieces(i, &count);
        /* #embed's bytes: one line, as clang's annot_embed */
        if (dump_embedded(files, piece[0])) {
            while (dump_embedded(files, tokens[i + 1].preprocessing))
                i++;
            vbuffer_string_append(out, "annot_embed ''\tLoc=<embed>\n");
            continue;
        }
        for (size_t k = 0; k < count; k++) {
            /* pieces() is the stream's own storage, which a later token may
               move: a copy */
            const preprocessing_token p = tokens.pieces(i, &count)[k];
            const char *name = clang_token_name(l, t);
            if (count > 1)
                name = clang_token_name(l, p);
            spelling.clear();
            if (p.kind != preprocessing_token_kind::end_of_file)
                files.spelling(p, spelling.get());
            const source_files::entry *e = files.find(p.location);
            source_location place = p.location;
            const unsigned char *physical = nullptr;
            if (p.expansion)
                place = p.expansion;
            else if (e)
                physical = e->file.physical_characters + p.location - e->base;
            dump_token(out, name, spelling.data(), spelling.size(),
                       files.position(place), &files, p, false, physical);
        }
        if (tokens.at_end(i))
            break;
    }
}

} // namespace cplusplus
