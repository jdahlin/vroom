/*
 * cxxlex: the C++ front end's lexer (src/frontend/cplusplus/lex) on one
 * source file, printing what clang -cc1 prints for the same command line, so
 * that bench/cxx-lex.py can compare the two:
 *
 *   cxxlex -dump-raw-tokens [OPTIONS] FILE   phase 3: preprocessing tokens,
 *                                            white space and comments
 *   cxxlex -dump-tokens [OPTIONS] FILE       phase 7: tokens (FILE should be
 *                                            preprocessed: line markers are
 *                                            followed, other directives skipped)
 *   cxxlex -literal-claims [OPTIONS] FILE    the same tokens' literals as a C++
 *                                            file of static_asserts: each
 *                                            literal's type and value as
 *                                            computed here, for clang to check
 *
 * OPTIONS are the clang -cc1 options that change lexing: -std=, -x,
 * -fms-extensions, -fms-compatibility, -fdollars-in-identifiers,
 * -fno-dollars-in-identifiers, -fchar8_t, -fno-char8_t, -trigraphs,
 * -ftrigraphs, -fno-trigraphs, -pedantic, -pedantic-errors, -W..., -w.  Others
 * are ignored.  Diagnostics go to stderr as clang prints them; the exit status
 * is 1 if there was an error.
 */
#include "command-line.h"

/*
 * A literal's type and value as static_asserts clang can check:
 *   _Static_assert(__is_same(__decltype(LIT), T), "");
 *   _Static_assert(LIT == (T)V, "");       integer and character literals
 *   _Static_assert(LIT[k] == (T)V_k, "");  each code unit of a string literal
 * with #line so that a failed claim names the literal's place (clang's
 * spellings, which every edition has).
 */
static void write_claim(vbuffer *out, const lexer &l, const token &t,
                        const char *spelling, size_t n, source_position p)
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

static bool is_string(const preprocessing_token &pp)
{
    return pp.kind == preprocessing_token_kind::string_literal ||
           pp.kind == preprocessing_token_kind::user_defined_string_literal;
}

int main(int argc, char **argv)
{
    command_line options("cxxlex");
    implementation_defined_behavior &behavior = options.behavior;
    diagnostic_options &diagnostic_settings = options.diagnostic_settings;
    preprocessor_options &pp_options = options.pp_options;
    bool raw = false, claims = false, preprocess = false, definitions = false;
    /* clang -cc1 -dump-tokens does not convert literals: phase 7's
       diagnostics are not issued (bench/cxx-pp.py) */
    bool phase4_only = false;
    const char *path = nullptr;

    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "-dump-raw-tokens"))
            raw = true;
        else if (!strcmp(a, "-dump-tokens"))
            raw = false;
        else if (!strcmp(a, "-literal-claims"))
            raw = false, claims = true;
        else if (!strcmp(a, "-phase4-diagnostics-only"))
            phase4_only = true;
        else if (!strcmp(a, "-E"))
            preprocess = true;
        else if (!strcmp(a, "-dM"))
            definitions = true;
        else if (options.option(argc, argv, i))
            ;
        else if (a[0] != '-' || !a[1])
            path = a;
    }
    if (!path) {
        fprintf(stderr, "usage: cxxlex [-dump-raw-tokens|-dump-tokens|-E|-E "
                        "-dM] [clang -cc1 options] FILE\n");
        return 2;
    }
    options.apply();
    diagnostic_consumer diagnostics(diagnostic_settings);
    owned_buffer out, spelling;
    auto finish = [&]() {
        fwrite(out.data(), 1, out.size(), stdout);
        owned_buffer messages;
        for (const diagnostic_message &m : diagnostics.messages)
            diagnostics.render(m, messages.get());
        fwrite(messages.data(), 1, messages.size(), stderr);
        if (diagnostics.error_count)
            return 1;
        return 0;
    };

    if (raw) {
        /* phase 3 alone, on the file as it is */
        source_file file;
        if (!file.open(path)) {
            fprintf(stderr, "cxxlex: cannot read %s\n", path);
            return 2;
        }
        diagnostics.file = &file;
        lexer_options options;
        options.retain_white_space = options.retain_comments = true;
        lexer l(file, behavior, diagnostics, options);
        for (preprocessing_token pp = l.next();
             pp.kind != preprocessing_token_kind::end_of_file; pp = l.next()) {
            spelling.clear();
            l.spelling(pp, spelling.get());
            write_token(out.get(), clang_token_name(l, pp), spelling.data(),
                        spelling.size(), file.position(pp.location), nullptr,
                        pp, true, file.physical_characters + pp.location);
        }
        return finish();
    }

    /* phases 1 to 4 */
    source_files files(behavior, diagnostics);
    diagnostics.files = &files;
    size_t path_length = strlen(path);
    files.file_options.preprocessed_input =
        (path_length > 3 && !strcmp(path + path_length - 3, ".ii")) ||
        (path_length > 2 && !strcmp(path + path_length - 2, ".i"));
    preprocessor pp(files, pp_options);
    if (!pp.start(path)) {
        fprintf(stderr, "cxxlex: cannot read %s\n", path);
        return 2;
    }
    owned_array<preprocessing_token> tokens;
    for (;;) {
        preprocessing_token t = pp.next();
        tokens.append(t);
        if (t.kind == preprocessing_token_kind::end_of_file)
            break;
    }
    if (definitions) {
        pp.write_definitions(out.get());
        return finish();
    }
    if (preprocess) {
        /* the preprocessing tokens, a new line where a line started, a
           space where white space was (clang -E -P without its spacing
           heuristics) */
        bool first = true;
        for (const preprocessing_token &t : tokens) {
            if (t.kind == preprocessing_token_kind::end_of_file)
                break;
            if (!first && t.at_start_of_line)
                vbuffer_append(out.get(), "\n", 1);
            else if (!first && t.after_white_space)
                vbuffer_append(out.get(), " ", 1);
            first = false;
            files.spelling(t, out.get());
        }
        if (!first)
            vbuffer_append(out.get(), "\n", 1);
        return finish();
    }

    /* phases 5 to 7 */
    const lexer &l = files.phase7();
    if (phase4_only)
        diagnostics.suppressed++;
    auto embedded = [&](const preprocessing_token &t) {
        const source_files::entry *e = files.find(t.location);
        return e && e->scratch && !strcmp(e->name.c_str(), "<embed>");
    };
    for (size_t i = 0; i < tokens.size(); i++) {
        const preprocessing_token &p = tokens[i];
        /* #embed's bytes ([ext.clang]): one line, as clang's annot_embed */
        if (!claims && embedded(p)) {
            while (i + 1 < tokens.size() && embedded(tokens[i + 1]))
                i++;
            vbuffer_string_append(out.get(), "annot_embed ''\tLoc=<embed>\n");
            continue;
        }
        /* [lex.phases]/1.6: adjacent string literals become one token; each
           piece is still printed as clang prints it */
        size_t pieces = 1;
        token t;
        if (is_string(p)) {
            while (i + pieces < tokens.size() && is_string(tokens[i + pieces]))
                pieces++;
            t = files.convert_string_literals(&tokens[i], pieces);
        } else
            t = files.convert(p);
        if (claims) {
            if (p.kind != preprocessing_token_kind::end_of_file) {
                /* the claim is about the concatenation, spelled as its pieces */
                spelling.clear();
                for (size_t k = 0; k < pieces; k++) {
                    if (k)
                        vbuffer_append(spelling.get(), " ", 1);
                    files.spelling(tokens[i + k], spelling.get());
                }
                write_claim(out.get(), l, t, spelling.data(), spelling.size(),
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
            write_token(out.get(), name, spelling.data(), spelling.size(),
                        files.position(place), &files, piece, false, physical);
        }
        i += pieces - 1;
    }
    return finish();
}
