/*
 * cpp.include.cpp: [cpp.include] Source file inclusion.
 *
 * [cpp.include]/1: "A #include directive shall identify a header or source
 * file that can be processed by the implementation."  The places searched
 * ([cpp.include]/2: "a sequence of implementation-defined places") are
 * clang's: for "q-char-sequence" the directory of the including file, then
 * -iquote's; for both forms then -I's, -isystem's and -idirafter's, each in
 * command line order.  A file is named as clang names it: the directory, a
 * slash and the q-char-sequence or h-char-sequence.
 */
#include <stdio.h>
#include <sys/stat.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

bool is_regular_file(const char *path, struct stat *st)
{
    return stat(path, st) == 0 && S_ISREG(st->st_mode);
}

/* the place of a directory in the search: -iquote first, then -I,
   -isystem, -idirafter, each in command line order */
unsigned search_rank(include_search s)
{
    return static_cast<unsigned>(s);
}

} // namespace

/*
 * [cpp.include]/2-3: the file named by a header-name's characters between
 * the delimiters, searched for from the directory after after_directory
 * (clang's #include_next), its path in `path`.  [cpp.include]/3: "If this
 * search is not supported, or if the search fails, the directive is
 * reprocessed as if it read # include < h-char-sequence > new-line".
 */
bool preprocessor::find_header(const char *name, size_t length, bool angled,
                               int32_t after_directory, owned_buffer &path,
                               int32_t *directory, bool *system)
{
    struct stat st;
    *directory = -1;
    *system = false;
    path.clear();
    if (length && name[0] == '/') {
        vbuffer_append(path.get(), name, length);
        vbuffer_append(path.get(), "", 1);
        return is_regular_file(path.data(), &st);
    }
    if (!angled && after_directory < 0 && !inclusions.empty()) {
        /* the directory of the file containing the directive */
        const char *includer = inclusions.back().file->name.c_str();
        const char *slash = strrchr(includer, '/');
        if (slash)
            vbuffer_append(path.get(), includer, (size_t)(slash - includer));
        else
            vbuffer_append(path.get(), ".", 1);
        vbuffer_append(path.get(), "/", 1);
        vbuffer_append(path.get(), name, length);
        vbuffer_append(path.get(), "", 1);
        if (is_regular_file(path.data(), &st)) {
            *system = inclusions.back().system_header;
            return true;
        }
    }
    const auto &dirs = options.include_directories;
    int32_t position = 0;
    for (unsigned rank = 0; rank < 4; rank++)
        for (size_t i = 0; i < dirs.size(); i++) {
            if (search_rank(dirs[i].search) != rank)
                continue;
            int32_t here = position++;
            if (here <= after_directory)
                continue;
            if (angled && dirs[i].search == include_search::quote)
                continue;
            path.clear();
            size_t n = strlen(dirs[i].path);
            vbuffer_append(path.get(), dirs[i].path, n);
            if (n && dirs[i].path[n - 1] != '/')
                vbuffer_append(path.get(), "/", 1);
            vbuffer_append(path.get(), name, length);
            vbuffer_append(path.get(), "", 1);
            if (is_regular_file(path.data(), &st)) {
                *directory = here;
                *system = dirs[i].search == include_search::system ||
                          dirs[i].search == include_search::after;
                return true;
            }
        }
    return false;
}

/* clang's #include_next: the search starts after the directory the
   including file was found in */
int32_t preprocessor::include_next_start(const preprocessing_token &name)
{
    if (inclusions.size() <= 1) {
        diagnostics.issue(diagnostic_id::pp_include_next_in_primary,
                          name.location);
        return -1;
    }
    int32_t d = inclusions.back().directory;
    if (d < 0)
        diagnostics.issue(diagnostic_id::pp_include_next_absolute_path,
                          name.location);
    return d;
}

/*
 * The header-name of [cpp.include]/4's form: "The preprocessing tokens
 * after include in the directive are processed just as in normal text ...
 * The method by which a sequence of preprocessing tokens between a < and a
 * > preprocessing token pair or a pair of " characters is combined into a
 * single header name preprocessing token is implementation-defined": as
 * clang, their spellings, a space where white space preceded a token.
 * Footnote 1: "adjacent string-literals are not concatenated".
 */
bool preprocessor::header_name_from_tokens(const preprocessing_token &first,
                                           owned_buffer &header,
                                           source_location *at)
{
    owned_array<preprocessing_token> line;
    line.append(first);
    for (preprocessing_token t = directive_token(); !is_end(t);
         t = directive_token())
        line.append(t);
    size_t depth = contexts.size();
    push_tokens(line.data(), line.size(), -1, true);
    owned_array<preprocessing_token> expanded;
    for (preprocessing_token t; read_expanded(t);)
        expanded.append(t);
    while (contexts.size() > depth)
        pop_context();
    header.clear();
    if (expanded.empty()) {
        diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                          directive_end);
        return false;
    }
    *at = expanded[0].location;
    size_t used = 1;
    if (expanded[0].kind == preprocessing_token_kind::string_literal &&
        expanded[0].prefix == encoding_prefix::none && !expanded[0].raw)
        files.spelling(expanded[0], header.get());
    else if (is_operator(expanded[0], preprocessing_op_or_punc::LESS)) {
        vbuffer_append(header.get(), "<", 1);
        for (;; used++) {
            if (used == expanded.size()) {
                diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                                  expanded[0].location);
                return false;
            }
            const preprocessing_token &t = expanded[used];
            if (is_operator(t, preprocessing_op_or_punc::GREATER)) {
                vbuffer_append(header.get(), ">", 1);
                used++;
                break;
            }
            if (t.after_white_space && used > 1)
                vbuffer_append(header.get(), " ", 1);
            files.spelling(t, header.get());
        }
    } else {
        diagnostics.issue(diagnostic_id::err_pp_expects_filename,
                          expanded[0].location);
        return false;
    }
    if (used < expanded.size())
        diagnostics.issue(diagnostic_id::ext_pp_extra_tokens_at_eol,
                          expanded[used].location)
            << "include";
    return true;
}

/*
 * [cpp.include]/2: "# include < h-char-sequence > new-line ... causes the
 * replacement of that directive by the entire contents of the header";
 * /3 the same for "q-char-sequence"; /4 the form with pp-tokens.  Clang's
 * #include_next and #import (included once) go through here too.
 */
void preprocessor::directive_include(const preprocessing_token &name,
                                     directive_kind kind)
{
    owned_buffer directive;
    files.spelling(name, directive.get());
    char directive_text[16];
    snprintf(directive_text, sizeof directive_text, "%.*s",
             (int)directive.size(), directive.data());
    if (kind == directive_kind::include_next)
        diagnostics.issue(diagnostic_id::ext_pp_include_next_directive,
                          name.location);
    if (kind == directive_kind::import)
        diagnostics.issue(diagnostic_id::ext_pp_import_directive,
                          name.location);

    preprocessing_token t = directive_token(true);
    owned_buffer header;
    source_location at = t.location;
    if (is_end(t)) {
        diagnostics.issue(diagnostic_id::err_pp_expects_filename, t.location);
        return;
    }
    if (t.kind == preprocessing_token_kind::header_name) {
        files.spelling(t, header.get());
        expect_end_of_directive(directive_text);
    } else if (!header_name_from_tokens(t, header, &at))
        return;
    if (header.size() <= 2) {
        diagnostics.issue(diagnostic_id::err_pp_empty_filename, at);
        return;
    }
    bool angled = header.data()[0] == '<';
    int32_t after = -1;
    if (kind == directive_kind::include_next)
        after = include_next_start(name);

    owned_buffer path;
    int32_t directory;
    bool system;
    if (!find_header(header.data() + 1, header.size() - 2, angled, after, path,
                     &directory, &system)) {
        diagnostics.issue(diagnostic_id::err_pp_file_not_found, at)
            .text(header.data() + 1, header.size() - 2);
        return;
    }
    /* [cpp.include]/6: "up to an implementation-defined nesting limit" */
    if (inclusions.size() > options.include_nesting_limit) {
        diagnostics.issue(diagnostic_id::err_pp_include_too_deep, at);
        return;
    }
    struct stat st;
    stat(path.data(), &st);
    for (const included_once &o : once_files)
        if (o.device == (uint64_t)st.st_dev && o.inode == (uint64_t)st.st_ino)
            return;
    source_files::entry *file = files.open(path.data());
    if (!file) {
        diagnostics.issue(diagnostic_id::err_pp_file_not_found, at)
            .text(header.data() + 1, header.size() - 2);
        return;
    }
    if (kind == directive_kind::import)
        once_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
    file->system_header = system;
    inclusions.append({file,
                       directory,
                       (uint32_t)conditionals.size(),
                       name.location,
                       system,
                       false,
                       {}});
}

/* #pragma once: the current file is not included again */
void preprocessor::include_once()
{
    struct stat st;
    if (stat(inclusions.back().file->name.c_str(), &st) == 0)
        once_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
}

} // namespace cplusplus
