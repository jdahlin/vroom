/*
 * embed.cpp: [ext:clang_embed] C23's #embed (6.10.4), which C++26 adopted
 * as [cpp.embed] (P1967R14), in every edition, with a warning:
 *
 *     control-line:
 *         # embed pp-tokens new-line
 *
 * whose pp-tokens are, after macro replacement where they are not already,
 * a header-name and the embed parameters:
 *
 * replaced by the bytes of the resource as a comma-delimited list of
 * integer literals, between the prefix and suffix parameters' tokens, or
 * by the if_empty parameter's tokens when there are none.  The parameters:
 *
 *     limit ( constant-expression )     at most this many bytes
 *     prefix ( pp-tokens_opt )          before the bytes, if there are any
 *     suffix ( pp-tokens_opt )          after them
 *     if_empty ( pp-tokens_opt )        instead of them, if there are none
 *     clang::offset ( constant-expression )   skip this many bytes first
 *
 * each also spelled __name__.  __has_embed (in if-section.cpp) uses the same
 * parameters: 0 if the resource is not found (or a parameter is unknown), 1
 * if it has bytes, 2 if it is empty.  The bytes are spelled in a piece of
 * scratch text named "<embed>", which -fcxx-dump=tokens shows as clang's one
 * annot_embed token.
 */
#include <stdio.h>
#include <sys/stat.h>

#include "../../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

/* the name of a parameter without __ __ around it */
void parameter_name(const char *s, size_t n, owned_buffer &out)
{
    if (n > 4 && s[0] == '_' && s[1] == '_' && s[n - 1] == '_' &&
        s[n - 2] == '_')
        s += 2, n -= 4;
    vbuffer_append(out.get(), s, n);
}

bool is_named(const owned_buffer &b, const char *word)
{
    return b.size() == strlen(word) && !memcmp(b.data(), word, b.size());
}

} // namespace

int preprocessor::parse_embed_parameters(const preprocessing_token *t, size_t n,
                                         embed_parameters &p, bool in_has_embed)
{
    using op = preprocessing_op_or_punc;
    for (size_t i = 0; i < n;) {
        if (!is_identifier(t[i])) {
            diagnostics.issue(diagnostic_id::err_pp_expected_after,
                              t[i].location)
                << "'#embed'"
                << "an embed parameter";
            return -1;
        }
        owned_buffer scope, name, spelling;
        files.spelling(t[i], spelling.get());
        source_location at = t[i].location;
        if (i + 2 < n && is_operator(t[i + 1], op::COLON_COLON) &&
            is_identifier(t[i + 2])) {
            parameter_name(spelling.data(), spelling.size(), scope);
            spelling.clear();
            files.spelling(t[i + 2], spelling.get());
            i += 2;
        }
        parameter_name(spelling.data(), spelling.size(), name);
        i++;
        /* ( balanced-token-sequence_opt ) */
        owned_array<preprocessing_token> inside;
        bool parenthesized = i < n && is_operator(t[i], op::LEFT_PARENTHESIS);
        if (parenthesized) {
            unsigned depth = 0;
            for (i++; i < n; i++) {
                if (is_operator(t[i], op::LEFT_PARENTHESIS))
                    depth++;
                else if (is_operator(t[i], op::RIGHT_PARENTHESIS) &&
                         depth-- == 0)
                    break;
                inside.append(t[i]);
            }
            if (i >= n) {
                diagnostics.issue(diagnostic_id::err_pp_expected_after,
                                  directive_end)
                    << "'('"
                    << "')'";
                return -1;
            }
            i++;
        }
        bool clang = scope.size() && is_named(scope, "clang");
        /* 1 limit, 2 prefix, 3 suffix, 4 if_empty, 5 clang::offset */
        static constexpr const char *standard[] = {"limit", "prefix", "suffix",
                                                   "if_empty"};
        int which = 0;
        if (!scope.size()) {
            for (int k = 0; k < 4; k++)
                if (is_named(name, standard[k]))
                    which = k + 1;
        } else if (clang && is_named(name, "offset"))
            which = 5;
        if (!which) {
            if (in_has_embed)
                return 0;
            owned_buffer full;
            if (scope.size()) {
                vbuffer_append(full.get(), scope.data(), scope.size());
                vbuffer_append(full.get(), "::", 2);
            }
            vbuffer_append(full.get(), name.data(), name.size());
            (diagnostics.issue(diagnostic_id::err_pp_unknown_parameter, at)
             << 1)
                .text(full.data(), full.size());
            return -1;
        }
        bool *seen[] = {nullptr,       &p.has_limit,    &p.has_prefix,
                        &p.has_suffix, &p.has_if_empty, &p.has_offset};
        if (*seen[which]) {
            owned_buffer full;
            if (scope.size()) {
                vbuffer_append(full.get(), scope.data(), scope.size());
                vbuffer_append(full.get(), "::", 2);
            }
            vbuffer_append(full.get(), name.data(), name.size());
            diagnostics.issue(diagnostic_id::err_pp_embed_dup_params, at)
                .text(full.data(), full.size());
            return -1;
        }
        *seen[which] = true;
        if (!parenthesized) {
            diagnostics.issue(diagnostic_id::err_pp_expected_after, at)
                << "embed parameter"
                << "'('";
            return -1;
        }
        if (which == 1 || which == 5) {
            /* C23 6.10.4.2: "The constant expression shall not contain the
               defined unary operator"; the value is not negative */
            for (const preprocessing_token &k : inside)
                if (spelled_as(files, k, "defined")) {
                    diagnostics.issue(diagnostic_id::err_defined_in_pp_embed,
                                      k.location);
                    return -1;
                }
            uint64_t value;
            bool negative;
            if (!evaluate_expression(inside.data(), inside.size(), &value,
                                     &negative))
                return -1;
            if (negative) {
                char text[24];
                snprintf(text, sizeof text, "%lld", (long long)value);
                (diagnostics.issue(diagnostic_id::err_requires_positive_value,
                                   inside[0].location)
                 << text)
                    << 0;
                return -1;
            }
            if (which == 1)
                p.limit = value;
            else
                p.offset = value;
            continue;
        }
        owned_array<preprocessing_token> *to = &p.if_empty;
        if (which == 2)
            to = &p.prefix;
        else if (which == 3)
            to = &p.suffix;
        for (const preprocessing_token &k : inside)
            to->append(k);
    }
    return 1;
}

/* the resource: as a header ([cpp.include]'s places), else in the
   --embed-dir directories, in command line order */
bool preprocessor::find_embed(const char *name, size_t length, bool angled,
                              owned_buffer &path)
{
    int32_t directory;
    bool system;
    if (find_header(name, length, angled, -1, path, &directory, &system))
        return true;
    for (const char *d : options.embed_directories) {
        path.clear();
        vbuffer_format_append(path.get(), "%s/%.*s", d, (int)length, name);
        vbuffer_append(path.get(), "", 1);
        struct stat st;
        if (stat(path.data(), &st) == 0)
            return true;
    }
    return false;
}

/* the number of bytes the parameters select from the resource */
bool preprocessor::embedded_size(const char *path, const embed_parameters &p,
                                 uint64_t *size)
{
    struct stat st;
    if (stat(path, &st) != 0)
        return false;
    uint64_t n = (uint64_t)st.st_size;
    if (p.offset < n)
        n -= p.offset;
    else
        n = 0;
    if (p.has_limit && p.limit < n)
        n = p.limit;
    *size = n;
    return true;
}

void preprocessor::clang_embed_directive(const preprocessing_token &name)
{
    diagnostics.issue(diagnostic_id::ext_pp_embed_directive, name.location)
        << 1;
    if (collecting_arguments) {
        diagnostics.issue(diagnostic_id::err_embedded_directive, name.location)
            << 1 << "embed";
        skip_rest_of_directive();
        return;
    }
    /* the header-name, or pp-tokens that form one after macro replacement,
       then the parameters */
    owned_array<preprocessing_token> line;
    preprocessing_token first = directive_token(true);
    for (preprocessing_token t = first; !is_end(t); t = directive_token())
        line.append(t);
    if (line.empty()) {
        diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                          first.location);
        return;
    }
    owned_buffer header;
    size_t parameters_at = 1;
    owned_array<preprocessing_token> tokens;
    if (line[0].kind == preprocessing_token_kind::header_name) {
        files.spelling(line[0], header.get());
        for (const preprocessing_token &t : line)
            tokens.append(t);
    } else {
        size_t depth = contexts.size();
        push_tokens(line.data(), line.size(), -1, true);
        for (preprocessing_token t; read_expanded(t);)
            tokens.append(t);
        while (contexts.size() > depth)
            pop_context();
        if (!tokens.empty() && is_string_literal(tokens[0]))
            files.spelling(tokens[0], header.get());
        else if (!tokens.empty() &&
                 is_operator(tokens[0], preprocessing_op_or_punc::LESS)) {
            vbuffer_append(header.get(), "<", 1);
            for (; parameters_at < tokens.size(); parameters_at++) {
                const preprocessing_token &t = tokens[parameters_at];
                if (is_operator(t, preprocessing_op_or_punc::GREATER))
                    break;
                if (t.after_white_space && parameters_at > 1)
                    vbuffer_append(header.get(), " ", 1);
                files.spelling(t, header.get());
            }
            if (parameters_at == tokens.size()) {
                diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                                  tokens[0].location);
                return;
            }
            vbuffer_append(header.get(), ">", 1);
            parameters_at++;
        } else {
            source_location at = first.location;
            if (!tokens.empty())
                at = tokens[0].location;
            diagnostics.issue(diagnostic_id::err_pp_expects_filename, at);
            return;
        }
    }
    embed_parameters p;
    if (parse_embed_parameters(tokens.data() + parameters_at,
                               tokens.size() - parameters_at, p, false) < 0)
        return;
    if (header.size() <= 2) {
        diagnostics.issue(diagnostic_id::err_pp_empty_filename, first.location);
        return;
    }
    owned_buffer path;
    int32_t directory;
    bool system;
    (void)directory, (void)system;
    if (!find_embed(header.data() + 1, header.size() - 2,
                    header.data()[0] == '<', path)) {
        diagnostics.issue(diagnostic_id::err_pp_file_not_found, first.location)
            .text(header.data() + 1, header.size() - 2);
        return;
    }
    struct stat st;
    if (stat(path.data(), &st) == 0 && !S_ISREG(st.st_mode)) {
        diagnostics.issue(diagnostic_id::err_pp_embed_device_file,
                          first.location);
        return;
    }
    /* the file went away after find_embed saw it: not found, as there */
    uint64_t size;
    if (!embedded_size(path.data(), p, &size)) {
        diagnostics.issue(diagnostic_id::err_pp_file_not_found, first.location)
            .text(header.data() + 1, header.size() - 2);
        return;
    }

    owned_array<preprocessing_token> out;
    auto append = [&](const owned_array<preprocessing_token> &from) {
        for (preprocessing_token t : from) {
            t.at_start_of_line = false;
            out.append(t);
        }
    };
    if (size == 0)
        append(p.if_empty);
    else {
        append(p.prefix);
        owned_buffer text;
        FILE *f = fopen(path.data(), "rb");
        if (f && fseek(f, (long)p.offset, SEEK_SET) == 0) {
            for (uint64_t i = 0; i < size; i++) {
                int c = fgetc(f);
                if (c == EOF)
                    break;
                if (i)
                    vbuffer_append(text.get(), ",", 1);
                vbuffer_format_append(text.get(), "%d", c);
            }
        }
        if (f)
            fclose(f);
        source_files::entry &e =
            files.add_text("<embed>", text.data(), text.size(), true);
        for (preprocessing_token t = files.next(e);
             t.kind != preprocessing_token_kind::end_of_file;
             t = files.next(e)) {
            t.at_start_of_line = t.after_white_space = false;
            out.append(t);
        }
        append(p.suffix);
    }
    if (!out.empty()) {
        push_tokens(out.data(), out.size(), -1, false);
        /* [impl.clang] the replacement continues the line: the token after
           it does not start one */
        embed_continues_line = true;
    }
}

} // namespace cplusplus
