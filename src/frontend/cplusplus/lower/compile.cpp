/*
 * compile.cpp: one C++ translation unit for the driver
 * (include/frontend/cplusplus/compile.h; doc/PLAN-cxx-sema.md S1e).
 *
 * The driver has already resolved the command line as it does for C: the
 * predefined macros (clang++'s, from -std=, -O and the target), -D, -U and
 * -include as the text of "<built-in>", and the include search path.  The
 * C++ preprocessor takes both as they are: its own predefined macros are
 * off (clang's -undef) and the driver's text is its command line, so C and
 * C++ see the same macros and headers.
 */
#include "../cplusplus-internal.h"
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

/* the driver's predefine text as the command line; of its built-in part
   (before the command line's -D/-U) not the macros the preprocessor
   defines itself without its predefined ones ([cpp.predefined]/1:
   __cplusplus, __STDC* ...), which would be redefinitions */
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
        if (!own)
            memcpy(pp.command_line.extend(size_t(next - s)), s,
                   size_t(next - s));
        s = next;
    }
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
    diagnostic_consumer diagnostics(settings);
    /* (source_files keeps a reference) */
    const implementation_defined_behavior behavior = compile_behavior(o, co);
    source_files files(behavior, diagnostics);
    diagnostics.files = &files;
    preprocessor_options options;
    options.predefined_macros = false;
    compile_predefines(o, options);
    compile_search_path(o->search, options);
    preprocessor pp(files, options);
    if (!pp.start(o->path)) {
        dprintf(o->errfd, "vcc: error: cannot read %s\n", o->path);
        return 1;
    }
    token_stream tokens(pp);
    syntax_tree tree;
    declared_names names;
    parser parse(tokens, tree, names, diagnostics);
    parse.translation_unit();
    semantic_analysis analysis(tree, tokens, parse, diagnostics);
    analysis.translation_unit();

    int status = 0;
    /* (-fsyntax-only: no code, nothing lowered) */
    if (!diagnostics.error_count && co->emit) {
        cc *c = ast_builder_open(p, co);
        lowering_failure why;
        if (!lower_translation_unit(analysis, tokens, c, co->target, why)) {
            source_position at = tokens.position(why.at);
            dprintf(o->errfd,
                    "%s:%u:%u: error: C++ code generation does not handle "
                    "%s yet\n",
                    at.file_name, at.line, at.column, why.what);
            status = 1;
        } else if (ast_builder_emit(c))
            status = 1;
        ast_builder_close(c);
    }
    owned_buffer messages;
    for (const diagnostic_message &m : diagnostics.messages)
        diagnostics.render(m, messages.get());
    if (messages.size() &&
        write(o->errfd, messages.data(), messages.size()) < 0)
        status = 1;
    if (diagnostics.error_count)
        status = 1;
    return status;
}

} // namespace cplusplus

int cplusplus_compile(const cplusplus_compile_options *o, pp *p,
                      const cc_options *co)
{
    return cplusplus::compile(o, p, co);
}
