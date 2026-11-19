/*
 * pragma-diagnostic.cpp: [ext:gnu_pragmas]: #pragma GCC diagnostic, also
 * spelled #pragma clang diagnostic (GCC manual 6.62.11, Diagnostic Pragmas):
 *
 *     #pragma GCC diagnostic ignored "-Wgroup"   (or warning, error, fatal)
 *     #pragma GCC diagnostic push
 *     #pragma GCC diagnostic pop
 *
 * the severity of a warning group's diagnostics from the pragma on.  Phase
 * 4 issues its diagnostics in the order of the source, so the mapping in
 * effect when one is issued is the one at its place.
 */
#include <stdio.h>

#include "../../cplusplus-internal.h"

namespace cplusplus
{

void preprocessor::gnu_pragma_diagnostic(const preprocessing_token *t,
                                         size_t count)
{
    /* t[0] is "diagnostic"; the kind is not macro-replaced */
    /* [impl.clang] these warnings are at the end of the directive */
    if (count < 2 || !is_identifier(t[1])) {
        diagnostics.issue(diagnostic_id::warn_pragma_diagnostic_invalid,
                          pragma_end);
        return;
    }
    const preprocessing_token &kind = t[1];
    if (spelled_as(files, kind, "push") || spelled_as(files, kind, "pop")) {
        if (spelled_as(files, kind, "push"))
            diagnostics.push_mappings();
        else if (!diagnostics.pop_mappings())
            diagnostics.issue(diagnostic_id::warn_pragma_diagnostic_cannot_pop,
                              pragma_end);
        if (count > 2)
            diagnostics.issue(
                diagnostic_id::warn_pragma_diagnostic_invalid_token,
                t[2].location);
        return;
    }
    diagnostic_severity severity;
    if (spelled_as(files, kind, "ignored"))
        severity = diagnostic_severity::ignored;
    else if (spelled_as(files, kind, "warning"))
        severity = diagnostic_severity::warning;
    else if (spelled_as(files, kind, "error"))
        severity = diagnostic_severity::error;
    else if (spelled_as(files, kind, "fatal"))
        severity = diagnostic_severity::fatal;
    else {
        /* at the token after the kind */
        diagnostics.issue(diagnostic_id::warn_pragma_diagnostic_invalid,
                          location_of(t, count, 2, pragma_end));
        return;
    }
    /* the option: string literals, concatenated */
    owned_buffer option, piece;
    size_t i = 2;
    for (; i < count && is_string_literal(t[i]); i++) {
        if (!string_contents(t[i], piece))
            break;
        vbuffer_append(option.get(), piece.data(), piece.size());
    }
    if (i == 2) {
        (diagnostics.issue(diagnostic_id::err_expected_string_literal,
                           location_of(t, count, i, directive_end))
         << 0)
            << "pragma diagnostic";
        return;
    }
    if (i < count) {
        diagnostics.issue(diagnostic_id::warn_pragma_diagnostic_invalid_token,
                          t[i].location);
        return;
    }
    if (option.size() < 3 || option.data()[0] != '-' ||
        (option.data()[1] != 'W' && option.data()[1] != 'R')) {
        diagnostics.issue(diagnostic_id::warn_pragma_diagnostic_invalid_option,
                          t[2].location);
        return;
    }
    char group[64];
    snprintf(group, sizeof group, "%.*s", (int)option.size() - 2,
             option.data() + 2);
    if (!diagnostics.map_group(group, severity))
        diagnostics
            .issue(diagnostic_id::warn_pragma_diagnostic_unknown_warning,
                   t[2].location)
            .text(option.data(), option.size());
}

} // namespace cplusplus
