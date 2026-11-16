/*
 * cpp.error.cpp: [cpp.error] Error directive, and C++23's #warning
 * (P2437R1), [ext.clang] in every edition.
 *
 * [cpp.error]/1: "A preprocessing directive of the form # error
 * pp-tokens_opt new-line causes the implementation to produce a diagnostic
 * message that includes the specified sequence of preprocessing tokens, and
 * renders the program ill-formed."
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

/* the rest of the directive's logical line as written (phases 1 and 2
   applied), [impl.clang] as clang's message has it: its characters, comments included,
   leading spaces removed */
void preprocessor::rest_of_line(source_location from, owned_buffer &out)
{
    const source_files::entry *e = files.find(from);
    out.clear();
    if (!e)
        return;
    for (uint32_t at = from - e->base;;) {
        lexer::translation_character c = e->lex.peek(at);
        if (!c.extent || c.character == '\n' || c.character == '\r')
            break;
        if (out.size() || c.character != ' ')
            vbuffer_append(out.get(), &c.character, 1);
        at += c.extent;
    }
}

void preprocessor::directive_error(const preprocessing_token &name,
                                   bool warning)
{
    /* C++23's; [ext.clang] clang_later_edition_features before */
    diagnostic_id id = diagnostic_id::err_pp_hash_error;
    if (warning) {
        diagnostic_id edition = diagnostic_id::ext_pp_warning_directive;
        if (behavior.has(language_feature::warning_directive))
            edition = diagnostic_id::warn_cxx23_compat_warning_directive;
        diagnostics.issue(edition, name.location) << 1;
        id = diagnostic_id::pp_hash_warning;
    }
    owned_buffer message;
    rest_of_line(name.location + name.extent, message);
    diagnostics.issue(id, name.location).text(message.data(), message.size());
    diagnostics_suppressed quiet(diagnostics);
    skip_rest_of_directive();
}

} // namespace cplusplus
