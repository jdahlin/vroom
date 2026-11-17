/*
 * cpp.include.cpp: [cpp.include] Source file inclusion.
 *
 * [cpp.include]/1: "A #include directive shall identify a header or source
 * file that can be processed by the implementation."  The places searched
 * ([cpp.include]/2: "a sequence of implementation-defined places") are
 * [impl.clang] clang's: for "q-char-sequence" the directory of the including file, then
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
    /* a quoted name is searched for first beside the file that names it */
    const void *includer = nullptr;
    if (!angled && after_directory < 0 && !inclusions.empty())
        includer = inclusions.back().file;
    uint32_t hash = 2166136261u; /* FNV-1a */
    for (size_t i = 0; i < length; i++)
        hash = (hash ^ static_cast<unsigned char>(name[i])) * 16777619u;
    hash = hash ^ static_cast<uint32_t>(after_directory) * 0x9E3779B1u ^
           static_cast<uint32_t>(angled) ^
           static_cast<uint32_t>(reinterpret_cast<uintptr_t>(includer) >> 4);
    if (header_search_slots.size() <= header_searches.size() * 2) {
        size_t size = 256;
        if (header_search_slots.size())
            size = header_search_slots.size() * 2;
        header_search_slots.resize(size);
        for (uint32_t &slot : header_search_slots)
            slot = 0;
        for (size_t k = 0; k < header_searches.size(); k++) {
            size_t i = header_searches[k].hash & (size - 1);
            while (header_search_slots[i])
                i = (i + 1) & (size - 1);
            header_search_slots[i] = static_cast<uint32_t>(k + 1);
        }
    }
    size_t mask = header_search_slots.size() - 1;
    size_t slot = hash & mask;
    for (; header_search_slots[slot]; slot = (slot + 1) & mask) {
        const header_search &h = header_searches[header_search_slots[slot] - 1];
        if (h.hash != hash || h.name_length != length ||
            h.after_directory != after_directory || h.angled != angled ||
            h.includer != includer ||
            memcmp(header_search_text.data() + h.name_offset, name, length))
            continue;
        path.clear();
        vbuffer_append(path.get(), header_search_text.data() + h.path_offset,
                       h.path_length);
        *directory = h.directory;
        *system = h.system;
        found_device = h.device;
        found_inode = h.inode;
        return h.found;
    }
    header_search h{};
    h.hash = hash;
    h.includer = includer;
    h.after_directory = after_directory;
    h.angled = angled;
    h.found = search_header(name, length, angled, after_directory, path,
                            directory, system);
    h.system = *system;
    h.directory = *directory;
    h.device = found_device;
    h.inode = found_inode;
    h.name_offset = static_cast<uint32_t>(header_search_text.size());
    h.name_length = static_cast<uint32_t>(length);
    memcpy(header_search_text.extend(length), name, length);
    h.path_offset = static_cast<uint32_t>(header_search_text.size());
    h.path_length = static_cast<uint32_t>(path.size());
    memcpy(header_search_text.extend(path.size()), path.data(), path.size());
    header_searches.append(h);
    header_search_slots[slot] = static_cast<uint32_t>(header_searches.size());
    return h.found;
}

bool preprocessor::search_header(const char *name, size_t length, bool angled,
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
        bool found = is_regular_file(path.data(), &st);
        found_device = (uint64_t)st.st_dev;
        found_inode = (uint64_t)st.st_ino;
        return found;
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
            found_device = (uint64_t)st.st_dev;
            found_inode = (uint64_t)st.st_ino;
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
                found_device = (uint64_t)st.st_dev;
                found_inode = (uint64_t)st.st_ino;
                *directory = here;
                *system = dirs[i].search == include_search::system ||
                          dirs[i].search == include_search::after;
                return true;
            }
        }
    return false;
}

/*
 * The header-name of [cpp.include]/4's form: "The preprocessing tokens
 * after include in the directive are processed just as in normal text ...
 * The method by which a sequence of preprocessing tokens between a < and a
 * > preprocessing token pair or a pair of " characters is combined into a
 * single header name preprocessing token is implementation-defined": as
 * [impl.clang] clang, their spellings, a space where white space preceded a token.
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
 * /3 the same for "q-char-sequence"; /4 the form with pp-tokens.  The
 * extensions #include_next ([ext.gnu]) and #import ([ext.clang]) go
 * through here too.
 */
void preprocessor::directive_include(const preprocessing_token &name,
                                     directive_kind kind)
{
    owned_buffer directive;
    files.spelling(name, directive.get());
    char directive_text[16];
    snprintf(directive_text, sizeof directive_text, "%.*s",
             (int)directive.size(), directive.data());
    /* [ext.gnu] gnu_include_next, [ext.clang] clang_import_directive */
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
    if (kind == directive_kind::include_next) /* [ext.gnu] */
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
    if (include_limit_reached)
        return;
    if (inclusions.size() > options.include_nesting_limit) {
        /* [impl.clang] once, and no file is included after it (an
           inclusion cycle ends) */
        diagnostics.issue(diagnostic_id::err_pp_include_too_deep, at);
        include_limit_reached = true;
        cache_spoil(); /* OPTIMIZATION: the header cache */
        return;
    }
    /* OPTIMIZATION: the file find_header found, not asked again */
    struct {
        uint64_t st_dev, st_ino;
    } st = {found_device, found_inode};
    bool once = false;
    for (const included_once &o : once_files)
        if (o.device == (uint64_t)st.st_dev && o.inode == (uint64_t)st.st_ino)
            once = true;
    /* OPTIMIZATION: the header cache */
    cache_once_read((uint64_t)st.st_dev, (uint64_t)st.st_ino, once);
    if (once)
        return;
    if (kind == directive_kind::import)
        cache_spoil(); /* OPTIMIZATION: the header cache */
    /* [ext.clang] #import: not a file entered before, the main file too */
    if (kind == directive_kind::import)
        for (const included_once &o : entered_files)
            if (o.device == (uint64_t)st.st_dev &&
                o.inode == (uint64_t)st.st_ino)
                return;
    entered_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
    cache_effect(effect_entered, (uint64_t)st.st_dev, (uint64_t)st.st_ino, 0,
                 nullptr); /* OPTIMIZATION: the header cache */
    if (guard_skips((uint64_t)st.st_dev, (uint64_t)st.st_ino))
        return; /* OPTIMIZATION: the multiple-include guard */
    if (cache_replay((uint64_t)st.st_dev, (uint64_t)st.st_ino, directory,
                     system))
        return; /* OPTIMIZATION: the header cache */
    source_files::entry *file = files.open(path.data());
    if (!file) {
        diagnostics.issue(diagnostic_id::err_pp_file_not_found, at)
            .text(header.data() + 1, header.size() - 2);
        return;
    }
    if (kind == directive_kind::import) /* [ext.clang]: included once */
        once_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
    file->system_header = system;
    inclusions.append({file,
                       directory,
                       (uint32_t)conditionals.size(),
                       name.location,
                       system,
                       false,
                       {}});
    inclusion &entered = inclusions.back();
    entered.guard_state = 1;
    entered.device = (uint64_t)st.st_dev;
    entered.inode = (uint64_t)st.st_ino;
    cache_record(directory, system); /* OPTIMIZATION: the header cache */
}

/*
 * OPTIMIZATION: the multiple-include guard (clang's MultipleIncludeOpt,
 * the old preprocessor's guard skip in frontend/pp/include.cpp).  A file
 * whose every token and directive lies in one if-section opened by
 *
 *     #ifndef NAME
 *     ...
 *     #endif
 *
 * with no #elif or #else, makes nothing and changes nothing when included
 * again while NAME is defined: [cpp.cond]/13 skips the group, and the file
 * ends.  So it is not opened, read or skipped again.  The state of an
 * inclusion: 0 not watched (the main file, text), 1 nothing seen yet, 2 in
 * the guard's group, 3 after its #endif, 4 not guarded.  Removing these
 * hooks changes no output.
 */
void preprocessor::guard_token()
{
    inclusion &in = inclusions.back();
    if (in.guard_state == 1 || in.guard_state == 3)
        in.guard_state = 4;
}

void preprocessor::guard_directive(directive_kind kind)
{
    inclusion &in = inclusions.back();
    if (in.guard_state == 3)
        in.guard_state = 4;
    if (in.guard_state == 1 && kind != directive_kind::ifndef)
        in.guard_state = 4;
}

void preprocessor::guard_end_of_file()
{
    inclusion &in = inclusions.back();
    if (in.guard_state == 3) {
        guarded_files.append({in.device, in.inode, in.guard_name});
        if (!recordings.empty()) /* OPTIMIZATION: the header cache */
            cache_effect(effect_guard, in.device, in.inode,
                         cache_name(in.guard_name), nullptr);
    }
}

bool preprocessor::guard_skips(uint64_t device, uint64_t inode)
{
    for (const guarded_file &g : guarded_files)
        if (g.device == device && g.inode == inode)
            return macro_of(g.name) >= 0; /* the header cache */
    return false;
}

/* #pragma once: the current file is not included again */
void preprocessor::include_once()
{
    struct stat st;
    if (stat(inclusions.back().file->name.c_str(), &st) == 0) {
        once_files.append({(uint64_t)st.st_dev, (uint64_t)st.st_ino});
        cache_effect(effect_once, (uint64_t)st.st_dev, (uint64_t)st.st_ino, 0,
                     nullptr); /* OPTIMIZATION: the header cache */
    }
}

} // namespace cplusplus
