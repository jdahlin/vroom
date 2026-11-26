/*
 * pragma-directive.cpp: [cpp.pragma] Pragma directive.
 *
 * [cpp.pragma]/1: "A preprocessing directive of the form # pragma
 * pp-tokens_opt new-line causes the implementation to behave in an
 * implementation-defined manner. ... Any pragma that is not recognized by
 * the implementation is ignored."
 *
 * Every pragma is an implementation's.  Those of phase 4 that GCC and clang
 * have (once, push_macro, pop_macro, GCC poison, GCC system_header,
 * message, GCC warning, GCC error) are ext/gnu/pragmas.cpp; those of later
 * phases (pack, STDC, clang diagnostic, ...) are recognized and left here.
 * [impl.clang] Clang warns of the others with -Wunknown-pragmas.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

bool spelled_as(const source_files &files, const preprocessing_token &t,
                const char *word)
{
    if (!is_identifier(t))
        return false;
    owned_buffer b;
    files.spelling(t, b.get());
    return b.size() == strlen(word) && !memcmp(b.data(), word, b.size());
}

/* whether the token is the punctuator text */
static bool punctuator_is(const source_files &files,
                          const preprocessing_token &t, const char *text)
{
    owned_buffer b;
    files.spelling(t, b.get());
    return b.size() == strlen(text) && !memcmp(b.data(), text, b.size());
}

/* the first words of the pragmas clang knows, of any phase */
static bool known_pragma(preprocessor &p, const preprocessing_token &t)
{
    static constexpr const char *known[] = {"once",
                                            "push_macro",
                                            "pop_macro",
                                            "GCC",
                                            "clang",
                                            "STDC",
                                            "pack",
                                            "message",
                                            "comment",
                                            "weak",
                                            "redefine_extname",
                                            "omp",
                                            "unroll",
                                            "nounroll",
                                            "unroll_and_jam",
                                            "nounroll_and_jam",
                                            "align",
                                            "options",
                                            "ms_struct",
                                            "mark",
                                            "region",
                                            "endregion",
                                            "include_alias",
                                            "detect_mismatch",
                                            "execution_character_set",
                                            "warning",
                                            "optimize",
                                            "float_control",
                                            "fenv_access",
                                            "fp_contract",
                                            "managed",
                                            "init_seg",
                                            "section",
                                            "data_seg",
                                            "bss_seg",
                                            "const_seg",
                                            "code_seg",
                                            "intrinsic",
                                            "function",
                                            "alloc_text",
                                            "strict_gs_check",
                                            "loop",
                                            "vtordisp",
                                            "pointers_to_members",
                                            "export",
                                            "hdrstop",
                                            "visibility",
                                            "redefine_extname",
                                            "OPENCL",
                                            "acc",
                                            "FENV_ACCESS"};
    for (const char *k : known)
        if (spelled_as(p.files, t, k))
            return true;
    return false;
}

/*
 * The state of the pragmas that change the layout of classes: #pragma pack
 * ([ext.clang], GCC and MSVC's: "pack(N)" sets the packing, "pack()" resets
 * it, "pack(push[, id][, N])" and "pack(pop[, id])" a stack of it),
 * "ms_struct on|off|reset", "options align=NAME" and "align".  A pack
 * value is kept for the layout; the others have no rules.
 */
void preprocessor::layout_pragma_directive(const preprocessing_token *t,
                                           size_t count)
{
    /* the packing a number token sets */
    auto packing = [&](const preprocessing_token &n) -> uint8_t {
        for (unsigned value = 1; value <= 16; value *= 2) {
            char spelled[4];
            snprintf(spelled, sizeof spelled, "%u", value);
            if (punctuator_is(files, n, spelled))
                return static_cast<uint8_t>(value);
        }
        return 255;
    };
    if (spelled_as(files, t[0], "pack")) {
        /* "(" then the first operand */
        if (count < 3 || !punctuator_is(files, t[1], "(")) {
            if (count == 1)
                files.pack_pragma = 0;
        } else if (spelled_as(files, t[2], "push")) {
            files.pack_stack.append(files.pack_pragma);
            for (size_t i = 3; i < count; i++)
                if (t[i].kind == preprocessing_token_kind::pp_number)
                    files.pack_pragma = packing(t[i]);
        } else if (spelled_as(files, t[2], "pop")) {
            if (files.pack_stack.size()) {
                files.pack_pragma = files.pack_stack.back();
                files.pack_stack.resize(files.pack_stack.size() - 1);
            }
        } else if (t[2].kind == preprocessing_token_kind::pp_number) {
            files.pack_pragma = packing(t[2]);
        } else if (punctuator_is(files, t[2], ")")) {
            files.pack_pragma = 0;
        }
    } else if (spelled_as(files, t[0], "ms_struct")) {
        files.ms_struct_pragma = count > 1 && spelled_as(files, t[1], "on");
    } else if (spelled_as(files, t[0], "options")) {
        /* options align=reset ends it, any other alignment starts it */
        if (count > 3 && spelled_as(files, t[1], "align"))
            files.other_layout_pragma = !spelled_as(files, t[3], "reset");
    } else if (spelled_as(files, t[0], "align")) {
        files.other_layout_pragma = true;
    }
    if (files.ms_struct_pragma || files.other_layout_pragma)
        files.layout_pragma = 255;
    else
        files.layout_pragma = files.pack_pragma;
}

/* the pp-tokens of a pragma directive or of a _Pragma ([cpp.pragma.op]) */
void preprocessor::execute_pragma(const preprocessing_token *t, size_t count,
                                  source_location at)
{
    (void)at;
    if (!count)
        return;
    /* [ext:gnu_pragmas], gnu_push_pop_macro, gnu_pragma_message */
    if (gnu_pragma(t, count))
        return;
    if (spelled_as(files, t[0], "STDC") ||
        spelled_as(files, t[0], "float_control") ||
        spelled_as(files, t[0], "fenv_access") ||
        (count > 1 && spelled_as(files, t[0], "clang") &&
         spelled_as(files, t[1], "fp")))
        files.floating_point_pragma = true;
    layout_pragma_directive(t, count);
    if (!known_pragma(*this, t[0]))
        diagnostics.issue(diagnostic_id::warn_pragma_ignored, t[0].location);
}

void preprocessor::directive_pragma(const preprocessing_token &name)
{
    owned_array<preprocessing_token> tokens;
    for (preprocessing_token t = directive_token(); !is_end(t);
         t = directive_token())
        tokens.append(t);
    /* where the directive's new-line is, after any comment: clang's end of
       a directive, where some of its pragma diagnostics are */
    pragma_end = directive_end;
    if (const source_files::entry *e = files.find(directive_end)) {
        uint32_t at = directive_end - e->base;
        while (at < e->file.size && e->file.physical_characters[at] != '\n' &&
               e->file.physical_characters[at] != '\r')
            at++;
        pragma_end = e->base + at;
    }
    execute_pragma(tokens.data(), tokens.size(), name.location);
}

} // namespace cplusplus
