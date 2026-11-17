/*
 * pragmas.cpp: the pragmas of phase 4 that GCC and clang have ([cpp.pragma]:
 * each behaves "in an implementation-defined manner"):
 *
 *   [ext.gnu] gnu_pragmas (GNU cpp manual 7, Pragmas)
 *     #pragma once                  the file is not included again
 *     #pragma GCC poison x y        x and y may no longer be used
 *     #pragma GCC system_header     the rest of the file is a system header
 *     #pragma GCC dependency "f"    the file f must exist
 *     #pragma GCC diagnostic ...    pragma_diagnostic.cpp
 *     #pragma GCC warning "text"    a warning, an error
 *     #pragma GCC error "text"
 *   [ext.gnu] gnu_push_pop_macro (GCC manual 6.62.13)
 *     #pragma push_macro("x")       the definition of x saved, restored
 *     #pragma pop_macro("x")
 *   [ext.gnu] gnu_pragma_message (GCC manual 6.62.11)
 *     #pragma message("text")       a warning
 *
 * clang spells GCC's also #pragma clang ...
 */
#include "../../cplusplus-internal.h"

namespace cplusplus
{

/* the string of push_macro("X") and pop_macro("X"), as an identifier */
static bool pragma_macro_name(preprocessor &p, const preprocessing_token *t,
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
        /* at the string, or the token before where it should be */
        size_t where = 2;
        if (count < 3)
            where = 1;
        p.diagnostics
            .issue(diagnostic_id::err_pragma_push_pop_macro_malformed,
                   location_of(t, count, where, at))
            .text(which.data(), which.size());
        return false;
    }
    return true;
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
                          location_of(t, count, i, directive_end))
            << kind;
        return;
    }
    diagnostic_id id = diagnostic_id::warn_pragma_message;
    if (kind == 2)
        id = diagnostic_id::err_pragma_message;
    diagnostics.issue(id, t[0].location).text(message.data(), message.size());
}

/* true if the pragma is one of these */
bool preprocessor::gnu_pragma(const preprocessing_token *t, size_t count)
{
    source_location at = t[0].location;
    if (spelled_as(files, t[0], "once") &&
        behavior.has(extension::gnu_pragmas)) {
        if (inclusions.size() <= 1)
            diagnostics.issue(diagnostic_id::pp_pragma_once_in_main_file,
                              t[0].location);
        else
            include_once();
        return true;
    }
    if ((spelled_as(files, t[0], "push_macro") ||
         spelled_as(files, t[0], "pop_macro")) &&
        behavior.has(extension::gnu_push_pop_macro)) {
        owned_buffer name;
        if (!pragma_macro_name(*this, t, count, at, name))
            return true;
        uint32_t id = identifier(name.data(), name.size());
        cache_spoil(); /* OPTIMIZATION: the header cache */
        if (spelled_as(files, t[0], "push_macro")) {
            pushed_macros.append({id, identifiers[id].macro});
            return true;
        }
        for (size_t i = pushed_macros.size(); i-- > 0;)
            if (pushed_macros[i].name == id) {
                identifiers[id].macro = pushed_macros[i].macro;
                for (size_t k = i + 1; k < pushed_macros.size(); k++)
                    pushed_macros[k - 1] = pushed_macros[k];
                pushed_macros.resize(pushed_macros.size() - 1);
                return true;
            }
        diagnostics
            .issue(diagnostic_id::warn_pragma_pop_macro_no_push, t[0].location)
            .text(name.data(), name.size());
        return true;
    }
    if (spelled_as(files, t[0], "message") &&
        behavior.has(extension::gnu_pragma_message)) {
        pragma_message(t, count, 0);
        return true;
    }
    bool gcc = spelled_as(files, t[0], "GCC"),
         clang = spelled_as(files, t[0], "clang");
    if (!(gcc || clang) || count < 2 || !behavior.has(extension::gnu_pragmas))
        return false;
    if (spelled_as(files, t[1], "diagnostic")) {
        gnu_pragma_diagnostic(t + 1, count - 1);
        return true;
    }
    if (spelled_as(files, t[1], "poison")) {
        for (size_t i = 2; i < count; i++) {
            if (!is_identifier(t[i])) {
                diagnostics.issue(diagnostic_id::err_pp_invalid_poison,
                                  t[i].location);
                return true;
            }
            uint32_t id = identifier(t[i]);
            if (identifiers[id].poisoned)
                continue;
            /* OPTIMIZATION: the header cache is not used from here on */
            cache_spoil();
            cache_disabled = true;
            if (identifiers[id].macro >= 0)
                diagnostics.issue(diagnostic_id::pp_poisoning_existing_macro,
                                  t[i].location);
            identifiers[id].poisoned = true;
        }
        return true;
    }
    if (spelled_as(files, t[1], "dependency")) {
        /* #pragma GCC dependency "file": the file must exist (GCC warns if
           it is newer than the current one; clang too, this does not) */
        owned_buffer header;
        if (count > 2 && is_string_literal(t[2]) &&
            t[2].prefix == encoding_prefix::none)
            files.spelling(t[2], header.get());
        else if (count > 2 &&
                 is_operator(t[2], preprocessing_op_or_punc::LESS)) {
            size_t i = 3;
            vbuffer_append(header.get(), "<", 1);
            for (; i < count &&
                   !is_operator(t[i], preprocessing_op_or_punc::GREATER);
                 i++)
                files.spelling(t[i], header.get());
            vbuffer_append(header.get(), ">", 1);
        } else {
            diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                              location_of(t, count, 2, directive_end));
            return true;
        }
        owned_buffer path;
        int32_t directory;
        bool system;
        if (header.size() > 2 &&
            !find_header(header.data() + 1, header.size() - 2,
                         header.data()[0] == '<', -1, path, &directory,
                         &system))
            diagnostics
                .issue(diagnostic_id::err_pp_file_not_found, t[2].location)
                .text(header.data() + 1, header.size() - 2);
        return true;
    }
    if (spelled_as(files, t[1], "system_header")) {
        if (inclusions.size() <= 1)
            diagnostics.issue(diagnostic_id::pp_pragma_sysheader_in_main_file,
                              t[1].location);
        else
            inclusions.back().file->system_header = true;
        return true;
    }
    if (gcc && (spelled_as(files, t[1], "warning") ||
                spelled_as(files, t[1], "error"))) {
        int kind = 2; /* GCC error */
        if (spelled_as(files, t[1], "warning"))
            kind = 1;
        pragma_message(t + 1, count - 1, kind);
        return true;
    }
    return false;
}

} // namespace cplusplus
