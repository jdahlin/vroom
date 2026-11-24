/*
 * compile.cpp: one C++ translation unit for the driver
 * (include/frontend/cplusplus/compile.h; doc/PLAN-cxx-sema.md S1e).
 *
 * The driver has already resolved the command line as it does for C: the
 * predefined macros (clang++'s, from -std=, -O and the target), -D, -U and
 * -include as the text of "<built-in>", and the include search path.  The
 * C++ preprocessor takes both as they are: its own predefined macros are
 * off (clang's -undef) and the driver's text is its command line, so C and
 * C++ see the same macros and headers.  The options that change how the
 * front end reads the file, and its developer outputs (-fcxx-dump=,
 * dump/dump.h), it reads itself from o->arguments.
 */
#include "../cplusplus-internal.h"
#include "../dump/dump.h"
#include "frontend/cplusplus/compile.h"
#include "frontend/cplusplus/lower.h"

namespace cplusplus
{

/* the implementation's choices for the target the driver compiles for */
static implementation_defined_behavior
compile_behavior(const cplusplus_compile_options *o, const cc_options *co)
{
    standard_edition edition = standard_edition::cplusplus_17;
    switch (o->edition) {
    case 98:
    case 3:
        edition = standard_edition::cplusplus_98;
        break;
    case 11:
        edition = standard_edition::cplusplus_11;
        break;
    case 14:
        edition = standard_edition::cplusplus_14;
        break;
    case 20:
        edition = standard_edition::cplusplus_20;
        break;
    case 23:
        edition = standard_edition::cplusplus_23;
        break;
    case 26:
        edition = standard_edition::cplusplus_26;
        break;
    default:
        break;
    }
    implementation_defined_behavior b =
        implementation_defined_behavior::for_edition(edition, o->gnu != 0);
    /* [basic.fundamental]/7, /8, /12: the target's char, wchar_t and long
       double, as clang's target info has them */
    b.plain_char_is_signed = !co->unsigned_char;
    if (co->target == PP_DARWIN) {
        b.wchar_t_is_signed = true;
        b.long_double_fraction_bits = 52;
    } else if (co->target == PP_LINUX_X86_64) {
        b.wchar_t_is_signed = true;
        b.long_double_fraction_bits = 63;
        b.vector_alignment_uncapped = true;
        b.va_list_is_array = true;
        b.bit_precise_maximum_width = 8388608;
    }
    return b;
}

/* the driver's search path: dirs[0..nquote) for "" only, then -I, then the
   system directories from nsys1 - 1 (-isystem, -idirafter: one search) */
static void compile_search_path(const pp_opts *s, preprocessor_options &pp)
{
    for (int i = 0; i < s->ndirs; i++) {
        include_search search = include_search::angled;
        if (i < s->nquote)
            search = include_search::quote;
        else if (s->nsys1 && i >= s->nsys1 - 1)
            search = include_search::system;
        pp.include_directories.append({s->dirs[i], search});
    }
}

/* the driver's predefine text: its built-in part (before the command
   line's -D/-U) after the preprocessor's own "<built-in>" macros, less
   those it defines itself without its predefined ones ([cpp.predefined]/1:
   __cplusplus, __STDC* ...), which would be redefinitions; the rest as the
   command line */
static void compile_predefines(const cplusplus_compile_options *o,
                               preprocessor_options &pp)
{
    const char *s = o->predefines, *end = s + o->predefines_length;
    const char *builtin_end = s + o->search->predef_cmdl;
    while (s < end) {
        const char *eol = static_cast<const char *>(memchr(s, '\n', end - s));
        const char *next = end;
        if (eol)
            next = eol + 1;
        static const char define[] = "#define ";
        const char *name = s + sizeof define - 1;
        bool own =
            s < builtin_end && name < next &&
            !memcmp(s, define, sizeof define - 1) &&
            (!strncmp(name, "__STDC", 6) || !strncmp(name, "__cplusplus ", 12));
        /* the driver's built-in macros in "<built-in>", the command
           line's -D, -U and -include in "<command line>", as clang
           reports their places */
        if (!own && s < builtin_end)
            memcpy(pp.built_in.extend(size_t(next - s)), s, size_t(next - s));
        else if (!own)
            memcpy(pp.command_line.extend(size_t(next - s)), s,
                   size_t(next - s));
        s = next;
    }
}

/*
 * What the analysis or the lowering has no rule for, at its token: the
 * subclause, and in a note the check in vcc that refused it (its source
 * file under src/, line and function), so the gap is found at once.
 */
static void report_failure(int fd, token_stream &tokens,
                           const lowering_failure &why)
{
    source_position at = tokens.position(why.at);
    if (why.subclause && why.detail && *why.detail)
        dprintf(fd,
                "%s:%u:%u: error: the rules of [%s] are not implemented "
                "yet: %s\n",
                at.file_name, at.line, at.column, why.subclause, why.detail);
    else if (why.subclause)
        dprintf(fd,
                "%s:%u:%u: error: the rules of [%s] are not implemented "
                "yet\n",
                at.file_name, at.line, at.column, why.subclause);
    else if (why.front_end)
        dprintf(fd,
                "%s:%u:%u: error: the C++ front end does not handle %s "
                "yet\n",
                at.file_name, at.line, at.column, why.what);
    else
        dprintf(fd,
                "%s:%u:%u: error: C++ code generation does not handle %s "
                "yet\n",
                at.file_name, at.line, at.column, why.what);
    if (why.file && *why.file) {
        const char *file = strstr(why.file, "/src/");
        if (file)
            file += 5;
        else
            file = why.file;
        dprintf(fd, "%s:%u:%u: note: refused by vcc's %s:%u (%s)\n",
                at.file_name, at.line, at.column, file, why.line, why.function);
    }
    if (why.expected) {
        /* where the parse of a declaration kept as tokens stopped: the
           grammar a later step parses, or tokens it could not read */
        source_position stop = tokens.position(why.parse_at);
        owned_buffer spelling;
        tokens.spelling(why.parse_at, spelling.get());
        dprintf(fd, "%s:%u:%u: note: the parser stopped at '%.*s': ",
                stop.file_name, stop.line, stop.column, (int)spelling.size(),
                spelling.data());
        if (why.later_step)
            dprintf(fd, "grammar it does not parse yet");
        else
            dprintf(fd, "expected %s", why.expected);
        if (why.parse_subclause && *why.parse_subclause)
            dprintf(fd, " ([%s])", why.parse_subclause);
        dprintf(fd, "\n");
    }
}

/* phases 7 to 9: the parse, the analysis, and unless -fsyntax-only the
   code; the developer outputs of the parse or of the analysis on out */
static int compile_translation_unit(const cplusplus_compile_options *o, pp *p,
                                    const cc_options *co,
                                    const dump_request &request,
                                    source_files &files, token_stream &tokens,
                                    diagnostic_consumer &diagnostics,
                                    vbuffer *out)
{
    (void)files;
    syntax_tree tree;
    declared_names names;
    parser parse(tokens, tree, names, diagnostics);
    parse.debug_fallbacks = request.debug_fallbacks;
    if (request.speculation_limit)
        parse.speculation_limit = request.speculation_limit;
    parse.translation_unit();
    if (request.phase() == dump_phase::parsing) {
        dump_parsing(request, tree, tokens, parse, out);
        return 0;
    }
    semantic_analysis analysis(tree, tokens, parse, diagnostics);
    analysis.translation_unit();
    if (request.phase() == dump_phase::analysis) {
        if (request.has(dump_output::type_claims))
            analysis.evaluate_analysed_expressions();
        dump_analysis(request, analysis, tree, tokens, out);
    }

    int status = 0;
    /* -fsyntax-only: no code, but a declaration analysis has no rules for
       is reported as -c reports it (the developer outputs report their
       own) */
    if (!diagnostics.error_count && !co->emit &&
        request.phase() == dump_phase::none) {
        lowering_failure why;
        if (!analysis_complete(analysis, why)) {
            report_failure(o->errfd, tokens, why);
            status = 1;
        }
    }
    if (!diagnostics.error_count && co->emit) {
        cc *c = ast_builder_open(p, co);
        lowering_failure why;
        if (!lower_translation_unit(analysis, tokens, c, co->target, why,
                                    o->path, co->rtti != 0)) {
            report_failure(o->errfd, tokens, why);
            status = 1;
        } else if (ast_builder_emit(c))
            status = 1;
        ast_builder_close(c);
    }
    return status;
}

/* the front end's own options (compile.h): what they change in behavior
   and settings, the outputs they ask for; false after an error on errfd */
static bool compile_arguments(const cplusplus_compile_options *o,
                              implementation_defined_behavior &behavior,
                              diagnostic_options &settings, dump_request &r)
{
    for (int i = 0; i < o->argument_count; i++) {
        const char *a = o->arguments[i];
        const char *bad = nullptr;
        if (!strncmp(a, "-fcxx-dump=", 11)) {
            if (!strcmp(a + 11, "help")) {
                owned_buffer help;
                dump_help(help.get());
                if (write(o->errfd, help.data(), help.size()) < 0)
                    return false;
                return false;
            }
            if (!dump_request_parse(a + 11, r, &bad)) {
                dprintf(o->errfd,
                        "vcc: error: unknown output '%s' in %s "
                        "(-fcxx-dump=help lists them)\n",
                        bad, a);
                return false;
            }
        } else if (!strcmp(a, "-fcxx-phase4-diagnostics-only"))
            r.phase4_diagnostics_only = true;
        else if (!strcmp(a, "-fcxx-dump-sections"))
            r.sections = true;
        else if (!strcmp(a, "-fcxx-debug-fallbacks"))
            r.debug_fallbacks = true;
        else if (!strncmp(a, "-fcxx-speculation-limit=", 24))
            r.speculation_limit = (uint32_t)strtoul(a + 24, nullptr, 10);
        else if (!strncmp(a, "-fcxx-", 6)) {
            dprintf(o->errfd, "vcc: error: unknown argument: '%s'\n", a);
            return false;
        } else if (!strcmp(a, "-fms-extensions"))
            behavior.microsoft_extensions = true;
        else if (!strcmp(a, "-fms-compatibility"))
            behavior.microsoft_extensions = behavior.microsoft_compatibility =
                true;
        else if (!strcmp(a, "-fchar8_t") || !strcmp(a, "-fno-char8_t"))
            behavior.char8_t_keyword = a[2] == 'c';
        else if (!strcmp(a, "-trigraphs") || !strcmp(a, "-ftrigraphs") ||
                 !strcmp(a, "-fno-trigraphs"))
            behavior.trigraphs = strcmp(a, "-fno-trigraphs") != 0;
        else if (!strcmp(a, "-fdollars-in-identifiers") ||
                 !strcmp(a, "-fno-dollars-in-identifiers"))
            behavior.dollar_in_identifier = a[2] == 'd';
        else if (!strcmp(a, "-fno-operator-names"))
            behavior.operator_names = false;
        else if (!strcmp(a, "--pedantic"))
            settings.parse("-pedantic");
        else if (!strcmp(a, "--pedantic-errors"))
            settings.parse("-pedantic-errors");
        else
            settings.parse(a);
    }
    /* the outputs of one phase: each takes the translation unit as far as
       its phase, and parser-tokens stops before the parse */
    dump_phase phase = r.phase();
    for (size_t i = 0; i < 32; i++) {
        dump_request one;
        one.outputs = r.outputs & (1u << i);
        if (one.outputs && one.phase() != phase &&
            one.phase() != dump_phase::none) {
            dprintf(o->errfd, "vcc: error: -fcxx-dump= asks for outputs of "
                              "different phases\n");
            return false;
        }
    }
    if (r.has(dump_output::parser_tokens) &&
        r.outputs !=
            (1u << static_cast<uint32_t>(dump_output::parser_tokens))) {
        dprintf(o->errfd, "vcc: error: -fcxx-dump=parser-tokens goes alone\n");
        return false;
    }
    return true;
}

static bool compile_write(int fd, const owned_buffer &b)
{
    const char *s = b.data();
    size_t n = b.size();
    while (n) {
        ssize_t k = write(fd, s, n);
        if (k <= 0)
            return false;
        s += k;
        n -= size_t(k);
    }
    return true;
}

static int compile(const cplusplus_compile_options *o, pp *p,
                   const cc_options *co)
{
    if (co->target != PP_LINUX_AARCH64 && co->target != PP_DARWIN &&
        co->target != PP_LINUX_X86_64) {
        dprintf(o->errfd, "vcc: error: C++ is not compiled for this target "
                          "yet\n");
        return 1;
    }
    diagnostic_options settings;
    implementation_defined_behavior behavior = compile_behavior(o, co);
    dump_request request;
    if (!compile_arguments(o, behavior, settings, request))
        return 1;
    owned_buffer out, messages;
    int status = 0;
    if (request.has(dump_output::selftest)) {
        if (!dump_selftest(out.get()))
            status = 1;
        if (!compile_write(co->outfd, out))
            status = 1;
        if (request.phase() == dump_phase::none)
            return status;
    }
    diagnostic_consumer diagnostics(settings);
    dump_phase phase = request.phase();
    if (phase == dump_phase::lexing) {
        if (!dump_raw_tokens(o->path, behavior, diagnostics, out.get(),
                             messages.get())) {
            dprintf(o->errfd, "vcc: error: cannot read %s\n", o->path);
            return 1;
        }
        if (!compile_write(co->outfd, out) ||
            !compile_write(o->errfd, messages) || diagnostics.error_count)
            status = 1;
        return status;
    }
    /* (source_files keeps a reference) */
    source_files files(behavior, diagnostics);
    diagnostics.files = &files;
    files.file_options.preprocessed_input = o->preprocessed_input != 0;
    preprocessor_options options;
    options.predefined_macros = false;
    compile_predefines(o, options);
    compile_search_path(o->search, options);
    for (int i = 0; i < o->embed_directory_count; i++)
        options.embed_directories.append(o->embed_directories[i]);
    preprocessor pp(files, options);
    if (!pp.start(o->path)) {
        dprintf(o->errfd, "vcc: error: cannot read %s\n", o->path);
        return 1;
    }
    if (phase == dump_phase::preprocessing)
        dump_preprocessing(request, files, pp, diagnostics, out.get());
    else {
        token_stream tokens(pp);
        if (request.has(dump_output::parser_tokens))
            dump_parser_tokens(request, files, tokens, diagnostics, out.get());
        else
            status = compile_translation_unit(o, p, co, request, files, tokens,
                                              diagnostics, out.get());
    }
    for (const diagnostic_message &m : diagnostics.messages)
        diagnostics.render(m, messages.get());
    if (!compile_write(co->outfd, out) || !compile_write(o->errfd, messages) ||
        diagnostics.error_count)
        status = 1;
    return status;
}

} // namespace cplusplus

int cplusplus_compile(const cplusplus_compile_options *o, pp *p,
                      const cc_options *co)
{
    return cplusplus::compile(o, p, co);
}
