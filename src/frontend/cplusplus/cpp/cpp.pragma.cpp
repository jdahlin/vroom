/*
 * cpp.pragma.cpp: [cpp.pragma] Pragma directive.
 *
 * [cpp.pragma]/1: "A preprocessing directive of the form # pragma
 * pp-tokens_opt new-line causes the implementation to behave in an
 * implementation-defined manner. ... Any pragma that is not recognized by
 * the implementation is ignored."
 *
 * The pragmas of phase 4 that clang has are executed here: once,
 * push_macro, pop_macro, GCC poison, GCC system_header, message, GCC
 * warning and GCC error.  Those of later phases (pack, STDC, clang
 * diagnostic, ...) are recognized and left; clang warns of the others
 * (-Wunknown-pragmas).
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

bool pragma_word(preprocessor &p, const preprocessing_token &t, const char *s)
{
    if (!is_identifier(t))
        return false;
    owned_buffer b;
    p.files.spelling(t, b.get());
    return b.size() == strlen(s) && !memcmp(b.data(), s, b.size());
}

/* the first words of the pragmas clang knows, of any phase */
bool known_pragma(preprocessor &p, const preprocessing_token &t)
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
        if (pragma_word(p, t, k))
            return true;
    return false;
}

} // namespace

/* the string of push_macro("X") and pop_macro("X"), as an identifier */
static bool macro_name_operand(preprocessor &p, const preprocessing_token *t,
                               size_t count, source_location at,
                               owned_buffer &name)
{
    if (count < 4 ||
        !is_operator(t[1], preprocessing_op_or_punc::LEFT_PARENTHESIS) ||
        !is_string_literal(t[2]) ||
        !is_operator(t[3], preprocessing_op_or_punc::RIGHT_PARENTHESIS) ||
        !p.string_contents(t[2], name)) {
        owned_buffer which;
        p.files.spelling(t[0], which.get());
        p.diagnostics
            .issue(diagnostic_id::err_pragma_push_pop_macro_malformed,
                   count > 1 ? t[count > 2 ? 2 : 1].location : at)
            .text(which.data(), which.size());
        return false;
    }
    return true;
}

/* the pp-tokens of a pragma directive or of a _Pragma ([cpp.pragma.op]) */
void preprocessor::execute_pragma(const preprocessing_token *t, size_t count,
                                  source_location at)
{
    if (!count)
        return;
    if (pragma_word(*this, t[0], "once")) {
        if (inclusions.size() <= 1)
            diagnostics.issue(diagnostic_id::pp_pragma_once_in_main_file,
                              t[0].location);
        else
            include_once();
        return;
    }
    if (pragma_word(*this, t[0], "push_macro") ||
        pragma_word(*this, t[0], "pop_macro")) {
        owned_buffer name;
        if (!macro_name_operand(*this, t, count, at, name))
            return;
        uint32_t id = identifier(name.data(), name.size());
        if (pragma_word(*this, t[0], "push_macro")) {
            pushed_macros.append({id, identifiers[id].macro});
            return;
        }
        for (size_t i = pushed_macros.size(); i-- > 0;)
            if (pushed_macros[i].name == id) {
                identifiers[id].macro = pushed_macros[i].macro;
                for (size_t k = i + 1; k < pushed_macros.size(); k++)
                    pushed_macros[k - 1] = pushed_macros[k];
                pushed_macros.resize(pushed_macros.size() - 1);
                return;
            }
        diagnostics
            .issue(diagnostic_id::warn_pragma_pop_macro_no_push, t[0].location)
            .text(name.data(), name.size());
        return;
    }
    bool gcc = pragma_word(*this, t[0], "GCC"),
         clang = pragma_word(*this, t[0], "clang");
    if ((gcc || clang) && count > 1) {
        if (pragma_word(*this, t[1], "poison")) {
            for (size_t i = 2; i < count; i++) {
                if (!is_identifier(t[i])) {
                    diagnostics.issue(diagnostic_id::err_pp_invalid_poison,
                                      t[i].location);
                    return;
                }
                uint32_t id = identifier(t[i]);
                if (identifiers[id].poisoned)
                    continue;
                if (identifiers[id].macro >= 0)
                    diagnostics.issue(
                        diagnostic_id::pp_poisoning_existing_macro,
                        t[i].location);
                identifiers[id].poisoned = true;
            }
            return;
        }
        if (pragma_word(*this, t[1], "system_header")) {
            if (inclusions.size() <= 1)
                diagnostics.issue(
                    diagnostic_id::pp_pragma_sysheader_in_main_file,
                    t[1].location);
            else
                inclusions.back().file->system_header = true;
            return;
        }
        if (gcc && (pragma_word(*this, t[1], "warning") ||
                    pragma_word(*this, t[1], "error"))) {
            pragma_message(t + 1, count - 1,
                           pragma_word(*this, t[1], "warning") ? 1 : 2);
            return;
        }
        return;
    }
    if (pragma_word(*this, t[0], "message")) {
        pragma_message(t, count, 0);
        return;
    }
    if (!known_pragma(*this, t[0]))
        diagnostics.issue(diagnostic_id::warn_pragma_ignored, t[0].location);
}

/* #pragma message("..."), GCC warning "...", GCC error "...": the string
   literals, concatenated, are the message */
void preprocessor::pragma_message(const preprocessing_token *t, size_t count,
                                  int kind)
{
    size_t i = 1;
    bool parenthesized =
        i < count &&
        is_operator(t[i], preprocessing_op_or_punc::LEFT_PARENTHESIS);
    if (parenthesized)
        i++;
    owned_buffer message, piece;
    size_t strings = 0;
    for (; i < count && is_string_literal(t[i]); i++, strings++) {
        if (!string_contents(t[i], piece))
            break;
        vbuffer_append(message.get(), piece.data(), piece.size());
    }
    if (!strings ||
        (parenthesized &&
         (i >= count ||
          !is_operator(t[i], preprocessing_op_or_punc::RIGHT_PARENTHESIS)))) {
        diagnostics.issue(diagnostic_id::err_pragma_message_malformed,
                          i < count ? t[i].location : directive_end)
            << kind;
        return;
    }
    diagnostics
        .issue(kind == 2 ? diagnostic_id::err_pragma_message
                         : diagnostic_id::warn_pragma_message,
               t[0].location)
        .text(message.data(), message.size());
}

void preprocessor::directive_pragma(const preprocessing_token &name)
{
    owned_array<preprocessing_token> tokens;
    for (preprocessing_token t = directive_token(); !is_end(t);
         t = directive_token())
        tokens.append(t);
    execute_pragma(tokens.data(), tokens.size(), name.location);
}

} // namespace cplusplus
