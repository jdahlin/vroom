/*
 * cpp.header_cache.cpp: OPTIMIZATION: the header cache.  Nothing in the
 * standard asks for it: [cpp.include]/2 replaces an #include "by the entire
 * contents of the header", and a later translation unit that includes the
 * same header in the same state gets the same contents.  This file keeps
 * them, after the old preprocessor's cache (frontend/pp/header-cache-*.cpp,
 * PLAN-cxx-codegen.md S0b), so that the translation units of one process
 * (cxxparse -batch, later vccd) run each header's directives and macro
 * replacement once.  Without a cache (preprocessor_options::cache null)
 * none of it runs, and with one the tokens are the same.
 *
 * Every #include whose file is entered at the top level (no replacement
 * in progress, no arguments being collected, nothing pending from a macro
 * replaced by nothing) is a region.  While a region runs for the first
 * time its inputs are recorded exactly:
 *
 *   - every read of an identifier's macro before the region writes it:
 *     the definition, named by where it was made (a definition is the
 *     same one where its name is at the same offset of the same source:
 *     a file of the same path and size, a text of the same name and
 *     contents), or "not defined";
 *   - every look at the #pragma once table;
 *
 * and its effects, in order: each definition and #undef, each source file
 * entered and text phase 4 made, the once, guard and #import tables'
 * additions, #pragma diagnostic's push, pop and mappings, and the nested
 * regions; then the tokens it returned, its own and its regions' (a rope:
 * a header's tokens are kept once however deep it is included).
 * Locations are kept in the cache's own space, where each source has one
 * place, and moved into a translation unit's space when replayed.
 *
 * A later #include of the same file found in the same place of the search
 * (the directory, and whether it is a system header), at the top level,
 * replays a variant whose inputs hold: its effects are applied (the
 * sources are entered again, so that their text and lines are there) and
 * its tokens are returned before the rest of the includer.  A region is not
 * kept when it issued a diagnostic (one a pragma of this translation unit
 * hid counts too), ended inside a macro invocation, or did what a replay
 * cannot repeat: #line and line markers, push_macro and pop_macro, GCC
 * poison, #import, #embed, module directives, __COUNTER__, __DATE__,
 * __TIME__, __TIMESTAMP__, __BASE_FILE__ and __INCLUDE_LEVEL__.  A region
 * not kept leaves its enclosing regions not kept.  The files are taken not
 * to change while the cache lives.
 */
#include "../cplusplus-internal.h"

namespace cplusplus
{

struct header_cache::entry {
    entry *next_variant;
    struct read {
        uint32_t name;
        uint64_t fingerprint;
    };
    owned_array<read> reads;
    struct once_read {
        uint64_t device, inode;
        bool present;
    };
    owned_array<once_read> once_reads;
    struct definition {
        uint32_t name;
        source_location location;
        uint32_t first_element, length, first_parameter;
        uint16_t parameter_count;
        bool function_like, variadic, named_variadic, undefined;
    };
    owned_array<definition> definitions;
    owned_array<replacement_element> elements;
    owned_array<uint32_t> parameters;
    struct source_event {
        uint32_t name_offset, name_length, text_offset, size, tu_index;
        bool text, system_header;
    };
    owned_array<source_event> events;
    owned_array<char> characters;
    struct effect {
        uint8_t kind;
        uint32_t index;
        uint64_t device, inode;
        const entry *child;
    };
    owned_array<effect> effects;
    owned_array<preprocessing_token> tokens;
    struct segment {
        uint32_t first, count;
        const entry *child;
    };
    owned_array<segment> segments;
    bool end_pending, end_start_of_line, end_white_space;
};

struct preprocessor::cache_recording {
    header_cache::entry *e;
    uint32_t serial;
    size_t inclusion_index, issued_at, files_mark, run_start;
    bool spoiled;
    uint64_t device, inode;
    int32_t directory;
    bool system;
};

namespace
{

constexpr uint64_t fingerprint_unknown = ~(uint64_t)0;

uint64_t hash_bytes(const void *p, size_t n, uint64_t h)
{
    const unsigned char *s = static_cast<const unsigned char *>(p);
    for (size_t i = 0; i < n; i++)
        h = (h ^ s[i]) * 0x100000001b3ull;
    return h;
}

/* open addressing: the slot of the entry equal(i) holds, or the empty one
   it would go in; slots hold index + 1 */
template <typename E>
uint32_t *probe(owned_array<uint32_t> &slots, uint64_t hash, E equal)
{
    size_t mask = slots.size() - 1;
    for (size_t i = hash & mask;; i = (i + 1) & mask) {
        uint32_t v = slots[i];
        if (!v || equal(v - 1))
            return &slots[i];
    }
}

/* room for one more of count entries, rehashed by hash_of(i) */
template <typename H>
void grow(owned_array<uint32_t> &slots, size_t count, H hash_of)
{
    if ((count + 1) * 2 <= slots.size())
        return;
    size_t n = 1024;
    while (n < (count + 1) * 2)
        n *= 2;
    slots.clear();
    memset(slots.extend(n), 0, n * sizeof(uint32_t));
    for (size_t i = 0; i < count; i++)
        *probe(slots, hash_of(i), [](uint32_t) { return false; }) =
            (uint32_t)i + 1;
}

/* a location of the translation unit in the cache's space (0: none) */
source_location to_cache(const source_files &files, source_location at)
{
    if (!at)
        return 0;
    const source_files::entry *e = files.find(at);
    if (!e || e->cache_source == UINT32_MAX || at - e->base > e->file.size)
        return 0;
    return files.cache->sources[e->cache_source].base + (at - e->base);
}

/* the other way: where the latest copy of the source is (0: none here) */
source_location from_cache(const source_files &files, source_location at)
{
    if (!at)
        return 0;
    const owned_array<header_cache::source> &s = files.cache->sources;
    size_t lo = 0, hi = s.size();
    while (hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if (s[mid].base <= at)
            lo = mid;
        else
            hi = mid;
    }
    if (lo >= files.cache_bases.size() || !files.cache_bases[lo]) {
        fprintf(stderr, "cxx header cache: source %zu (%.*s) is not here\n", lo,
                (int)s[lo].key_length,
                files.cache->debug_key(s[lo].key_offset));
        abort();
    }
    return files.cache_bases[lo] + (at - s[lo].base);
}

} // namespace

/* ---- the cache ---------------------------------------------------------- */

header_cache::header_cache() = default;
header_cache::~header_cache() = default;

uint32_t header_cache::source_of(const char *name, size_t name_length,
                                 const unsigned char *text, uint32_t size,
                                 bool is_text)
{
    uint64_t h = hash_bytes(name, name_length, 0xcbf29ce484222325ull);
    h = hash_bytes(&size, sizeof size, h);
    if (is_text)
        h = hash_bytes(text, size, h ^ 1);
    size_t key_length = name_length;
    if (is_text)
        key_length += 1 + size;
    grow(source_slots, sources.size(),
         [this](size_t i) { return sources[i].hash; });
    uint32_t *slot = probe(source_slots, h, [&](uint32_t i) {
        const source &s = sources[i];
        if (s.hash != h || s.size != size || s.text != is_text ||
            s.key_length != key_length)
            return false;
        const char *k = keys.data() + s.key_offset;
        if (memcmp(k, name, name_length))
            return false;
        return !is_text || !memcmp(k + name_length + 1, text, size);
    });
    if (*slot)
        return *slot - 1;
    source s{};
    s.key_offset = (uint32_t)keys.size();
    s.key_length = (uint32_t)key_length;
    s.size = size;
    s.hash = h;
    s.base = next_base;
    s.text = is_text;
    next_base += size + 1;
    char *k = keys.extend(key_length);
    memcpy(k, name, name_length);
    if (is_text) {
        k[name_length] = 0;
        memcpy(k + name_length + 1, text, size);
    }
    sources.append(s);
    *slot = (uint32_t)sources.size();
    return (uint32_t)sources.size() - 1;
}

uint32_t header_cache::name_of(const char *spelling, size_t length)
{
    uint32_t h = (uint32_t)hash_bytes(spelling, length, 0xcbf29ce484222325ull);
    grow(name_slots, names.size(), [this](size_t i) { return names[i].hash; });
    uint32_t *slot = probe(name_slots, h, [&](uint32_t i) {
        return names[i].hash == h && names[i].length == length &&
               !memcmp(name_characters.data() + names[i].offset, spelling,
                       length);
    });
    if (*slot)
        return *slot;
    names.append({(uint32_t)name_characters.size(), (uint32_t)length, h});
    memcpy(name_characters.extend(length), spelling, length);
    *slot = (uint32_t)names.size();
    return (uint32_t)names.size();
}

const char *header_cache::name_spelling(uint32_t n, size_t *length) const
{
    *length = names[n - 1].length;
    return name_characters.data() + names[n - 1].offset;
}

header_cache::entry *header_cache::find(uint64_t device, uint64_t inode,
                                        int32_t directory, bool system)
{
    for (const key &k : entry_keys)
        if (k.device == device && k.inode == inode &&
            k.directory == directory && k.system == system)
            return k.first;
    return nullptr;
}

header_cache::entry &header_cache::make()
{
    entry &e = entries.make();
    e.next_variant = nullptr;
    e.end_pending = e.end_start_of_line = e.end_white_space = false;
    return e;
}

void header_cache::keep(entry &e, uint64_t device, uint64_t inode,
                        int32_t directory, bool system)
{
    for (key &k : entry_keys)
        if (k.device == device && k.inode == inode &&
            k.directory == directory && k.system == system) {
            e.next_variant = k.first;
            k.first = &e;
            return;
        }
    entry_keys.append({device, inode, directory, system, &e});
}

/* ---- the preprocessor's hooks ------------------------------------------ */

bool preprocessor::cache_top_level() const
{
    return options.cache && !cache_disabled && contexts.empty() &&
           !collecting_arguments && !in_condition && !stop_at_file_end &&
           !pending_flags && !embed_continues_line &&
           replay_next >= replay_tokens.size();
}

uint32_t preprocessor::cache_name(uint32_t id)
{
    if (!identifiers[id].cache_name) {
        size_t length;
        const char *s = identifier_spelling(id, &length);
        identifiers[id].cache_name = options.cache->name_of(s, length);
    }
    return identifiers[id].cache_name;
}

uint32_t preprocessor::cache_identifier(uint32_t name)
{
    size_t have = cache_name_ids.size();
    if (have <= name) {
        size_t n = options.cache->name_count() + 1;
        memset(cache_name_ids.extend(n - have), 0,
               (n - have) * sizeof(uint32_t));
    }
    if (!cache_name_ids[name]) {
        size_t length;
        const char *s = options.cache->name_spelling(name, &length);
        uint32_t id = identifier(s, length);
        identifiers[id].cache_name = name;
        cache_name_ids[name] = id + 1;
    }
    return cache_name_ids[name] - 1;
}

/* a definition, as the same in every translation unit: where its name is
   in the cache's space; 0 for none, the kind for a dynamic one */
uint64_t preprocessor::cache_fingerprint(int32_t macro)
{
    if (macro < 0)
        return 0;
    const macro_definition &m = macros[macro];
    if (m.dynamic != dynamic_macro::none)
        return (uint64_t)1 << 63 | (uint64_t)m.dynamic;
    source_location at = to_cache(files, m.location);
    if (!at)
        return fingerprint_unknown;
    return at;
}

int32_t preprocessor::macro_of(uint32_t id)
{
    int32_t m = identifiers[id].macro;
    if (recordings.empty() ||
        identifiers[id].cache_serial >= recordings.back().serial)
        return m;
    uint64_t fingerprint = cache_fingerprint(m);
    uint32_t name = cache_name(id);
    /* the recordings opened after this identifier's latest one have not
       seen it; the earlier ones, open since, have */
    for (size_t i = recordings.size(); i-- > 0;) {
        cache_recording &r = recordings[i];
        if (identifiers[id].cache_serial >= r.serial)
            break;
        r.e->reads.append({name, fingerprint});
        if (fingerprint == fingerprint_unknown)
            r.spoiled = true;
    }
    identifiers[id].cache_serial = recordings.back().serial;
    return m;
}

void preprocessor::cache_write(uint32_t id, int32_t macro)
{
    if (recordings.empty())
        return;
    identifiers[id].cache_serial = recordings.back().serial;
    if (cache_applying)
        return;
    cache_recording &r = recordings.back();
    /* the sources first: the definition's tokens are in one of them */
    cache_collect_sources(r, files.size());
    header_cache::entry &e = *r.e;
    header_cache::entry::definition d{};
    d.name = cache_name(id);
    d.undefined = macro < 0;
    if (macro >= 0) {
        const macro_definition &m = macros[macro];
        d.location = to_cache(files, m.location);
        if (!d.location)
            r.spoiled = true;
        d.first_element = (uint32_t)e.elements.size();
        d.length = m.length;
        for (uint32_t i = 0; i < m.length; i++) {
            replacement_element x = elements[m.first_element + i];
            x.token.identifier = 0;
            source_location at = x.token.location,
                            expansion = x.token.expansion;
            x.token.location = to_cache(files, at);
            x.token.expansion = to_cache(files, expansion);
            if ((at && !x.token.location) || (expansion && !x.token.expansion))
                r.spoiled = true;
            e.elements.append(x);
        }
        d.first_parameter = (uint32_t)e.parameters.size();
        d.parameter_count = m.parameter_count;
        for (uint32_t i = 0; i < m.parameter_count; i++)
            e.parameters.append(cache_name(parameters[m.first_parameter + i]));
        d.function_like = m.function_like;
        d.variadic = m.variadic;
        d.named_variadic = m.named_variadic;
        if (m.predefined || m.dynamic != dynamic_macro::none || m.pushed >= 0)
            r.spoiled = true;
    }
    e.definitions.append(d);
    e.effects.append(
        {effect_write, (uint32_t)e.definitions.size() - 1, 0, 0, nullptr});
}

void preprocessor::cache_spoil()
{
    for (cache_recording &r : recordings)
        r.spoiled = true;
}

void preprocessor::cache_once_read(uint64_t device, uint64_t inode,
                                   bool present)
{
    for (cache_recording &r : recordings)
        r.e->once_reads.append({device, inode, present});
}

void preprocessor::cache_effect(uint8_t kind, uint64_t device, uint64_t inode,
                                uint32_t name, const char *text)
{
    if (recordings.empty() || cache_applying)
        return;
    header_cache::entry &e = *recordings.back().e;
    uint32_t index = name;
    if (text) {
        index = (uint32_t)e.characters.size();
        size_t n = strlen(text) + 1;
        memcpy(e.characters.extend(n), text, n);
    }
    e.effects.append({kind, index, device, inode, nullptr});
}

/* the sources placed since the recording last looked, up to end: entered
   again when the recording is replayed */
void preprocessor::cache_collect_sources(cache_recording &r, size_t end)
{
    header_cache::entry &e = *r.e;
    for (size_t i = r.files_mark; i < end; i++) {
        const source_files::entry &f = files[i];
        if (f.cache_source == UINT32_MAX)
            r.spoiled = true;
        header_cache::entry::source_event ev{};
        ev.name_offset = (uint32_t)e.characters.size();
        ev.name_length = (uint32_t)strlen(f.name.c_str());
        memcpy(e.characters.extend(ev.name_length + 1), f.name.c_str(),
               ev.name_length + 1);
        ev.text = f.scratch;
        ev.size = f.file.size;
        if (f.scratch) {
            ev.text_offset = (uint32_t)e.characters.size();
            memcpy(e.characters.extend(f.file.size), f.file.physical_characters,
                   f.file.size);
        }
        ev.tu_index = (uint32_t)i;
        e.events.append(ev);
        e.effects.append(
            {effect_source, (uint32_t)e.events.size() - 1, 0, 0, nullptr});
    }
    r.files_mark = end;
}

void preprocessor::cache_child_done(const void *child)
{
    const header_cache::entry *c =
        static_cast<const header_cache::entry *>(child);
    cache_recording &r = recordings.back();
    header_cache::entry &e = *r.e;
    if (e.tokens.size() > r.run_start)
        e.segments.append({(uint32_t)r.run_start,
                           (uint32_t)(e.tokens.size() - r.run_start), nullptr});
    e.segments.append({0, 0, c});
    r.run_start = e.tokens.size();
    e.effects.append({effect_child, 0, 0, 0, c});
    r.files_mark = files.size();
}

void preprocessor::cache_record(int32_t directory, bool system)
{
    if (!cache_top_level())
        return;
    const inclusion &in = inclusions.back();
    if (!recordings.empty())
        cache_collect_sources(recordings.back(), files.size() - 1);
    cache_recording r{};
    r.e = &options.cache->make();
    r.serial = ++cache_serial;
    r.inclusion_index = inclusions.size() - 1;
    r.issued_at = diagnostics.issued;
    r.files_mark = files.size() - 1;
    r.device = in.device;
    r.inode = in.inode;
    r.directory = directory;
    r.system = system;
    recordings.append(r);
    cache_collect_sources(recordings.back(), files.size());
}

void preprocessor::cache_file_end()
{
    if (recordings.empty() ||
        recordings.back().inclusion_index != inclusions.size() - 1)
        return;
    cache_collect_sources(recordings.back(), files.size());
    cache_recording r = recordings.back();
    header_cache::entry &e = *r.e;
    if (e.tokens.size() > r.run_start)
        e.segments.append({(uint32_t)r.run_start,
                           (uint32_t)(e.tokens.size() - r.run_start), nullptr});
    /* a #pragma GCC system_header met in the file */
    for (header_cache::entry::source_event &ev : e.events)
        ev.system_header = files[ev.tu_index].system_header;
    e.end_pending = pending_flags;
    e.end_start_of_line = pending_start_of_line;
    e.end_white_space = pending_white_space;
    bool kept = !r.spoiled && !cache_disabled &&
                diagnostics.issued == r.issued_at && contexts.empty() &&
                !collecting_arguments && !in_condition && !embed_continues_line;
    recordings.resize(recordings.size() - 1);
    if (!kept) {
        options.cache->spoiled++;
        if (!recordings.empty())
            recordings.back().spoiled = true;
        return;
    }
    options.cache->keep(e, r.device, r.inode, r.directory, r.system);
    options.cache->recorded++;
    if (!recordings.empty())
        cache_child_done(&e);
}

void preprocessor::cache_output(const preprocessing_token &t)
{
    if (recordings.empty())
        return;
    cache_recording &r = recordings.back();
    preprocessing_token c = t;
    c.identifier = 0;
    c.location = to_cache(files, t.location);
    c.expansion = to_cache(files, t.expansion);
    if ((t.location && !c.location) || (t.expansion && !c.expansion))
        r.spoiled = true;
    r.e->tokens.append(c);
}

bool preprocessor::cache_inputs_hold(const void *entry)
{
    const header_cache::entry &e =
        *static_cast<const header_cache::entry *>(entry);
    for (const header_cache::entry::read &x : e.reads) {
        uint32_t id = cache_identifier(x.name);
        if (cache_fingerprint(identifiers[id].macro) != x.fingerprint)
            return false;
    }
    for (const header_cache::entry::once_read &o : e.once_reads) {
        bool present = false;
        for (const included_once &f : once_files)
            if (f.device == o.device && f.inode == o.inode)
                present = true;
        if (present != o.present)
            return false;
    }
    return true;
}

void preprocessor::cache_apply(const void *entry)
{
    const header_cache::entry &e =
        *static_cast<const header_cache::entry *>(entry);
    for (const header_cache::entry::effect &x : e.effects) {
        switch (x.kind) {
        case effect_write: {
            const header_cache::entry::definition &d = e.definitions[x.index];
            uint32_t id = cache_identifier(d.name);
            if (d.undefined) {
                identifiers[id].macro = -1;
                cache_write(id, -1);
                break;
            }
            macro_definition m;
            m.name = id;
            m.location = from_cache(files, d.location);
            m.first_element = (uint32_t)elements.size();
            m.length = d.length;
            for (uint32_t i = 0; i < d.length; i++) {
                replacement_element r = e.elements[d.first_element + i];
                r.token.location = from_cache(files, r.token.location);
                r.token.expansion = from_cache(files, r.token.expansion);
                elements.append(r);
            }
            m.first_parameter = (uint32_t)parameters.size();
            m.parameter_count = d.parameter_count;
            for (uint32_t i = 0; i < d.parameter_count; i++)
                parameters.append(
                    cache_identifier(e.parameters[d.first_parameter + i]));
            m.function_like = d.function_like;
            m.variadic = d.variadic;
            m.named_variadic = d.named_variadic;
            add_definition(m);
            break;
        }
        case effect_source: {
            const header_cache::entry::source_event &ev = e.events[x.index];
            const char *name = e.characters.data() + ev.name_offset;
            if (ev.text)
                files.add_text(name, e.characters.data() + ev.text_offset,
                               ev.size, true);
            else if (!files.open(name)) {
                fprintf(stderr, "cxx header cache: %s is gone\n", name);
                abort();
            }
            files[files.size() - 1].system_header = ev.system_header;
            break;
        }
        case effect_once:
            once_files.append({x.device, x.inode});
            break;
        case effect_entered:
            entered_files.append({x.device, x.inode});
            break;
        case effect_guard:
            guarded_files.append(
                {x.device, x.inode, cache_identifier(x.index)});
            break;
        case effect_child:
            cache_apply(x.child);
            break;
        case effect_diagnostic_push:
            diagnostics.push_mappings();
            break;
        case effect_diagnostic_pop:
            diagnostics.pop_mappings();
            break;
        case effect_diagnostic_map:
            diagnostics.map_group(e.characters.data() + x.index,
                                  (diagnostic_severity)x.device);
            break;
        }
    }
}

void preprocessor::cache_flatten(const void *entry)
{
    const header_cache::entry &e =
        *static_cast<const header_cache::entry *>(entry);
    for (const header_cache::entry::segment &s : e.segments) {
        if (s.child) {
            cache_flatten(s.child);
            continue;
        }
        preprocessing_token *to = replay_tokens.extend(s.count);
        for (uint32_t i = 0; i < s.count; i++) {
            preprocessing_token t = e.tokens[s.first + i];
            t.location = from_cache(files, t.location);
            t.expansion = from_cache(files, t.expansion);
            to[i] = t;
        }
    }
}

bool preprocessor::cache_replay(uint64_t device, uint64_t inode,
                                int32_t directory, bool system)
{
    if (!cache_top_level())
        return false;
    header_cache &cache = *options.cache;
    const header_cache::entry *hit = nullptr;
    for (const header_cache::entry *v =
             cache.find(device, inode, directory, system);
         v && !hit; v = v->next_variant)
        if (cache_inputs_hold(v))
            hit = v;
    if (!hit) {
        cache.misses++;
        return false;
    }
    cache.hits++;
    if (!recordings.empty())
        cache_collect_sources(recordings.back(), files.size());
    /* the inputs: the enclosing recordings' too, and the definitions read
       are used */
    for (const header_cache::entry::read &x : hit->reads) {
        uint32_t id = cache_identifier(x.name);
        macro_of(id);
        int32_t m = identifiers[id].macro;
        if (m >= 0)
            macros[m].used = true;
    }
    for (const header_cache::entry::once_read &o : hit->once_reads)
        cache_once_read(o.device, o.inode, o.present);
    cache_applying++;
    cache_apply(hit);
    cache_applying--;
    size_t first = replay_tokens.size();
    cache_flatten(hit);
    cache.tokens_replayed += replay_tokens.size() - first;
    if (replay_tokens.size() == first) {
        if (hit->end_pending) {
            pending_flags = true;
            pending_start_of_line = hit->end_start_of_line;
            pending_white_space = hit->end_white_space;
        }
    } else {
        replay_pending_flags = hit->end_pending;
        replay_start_of_line = hit->end_start_of_line;
        replay_white_space = hit->end_white_space;
    }
    if (!recordings.empty())
        cache_child_done(hit);
    return true;
}

} // namespace cplusplus
