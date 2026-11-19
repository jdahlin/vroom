/*
 * intro.compliance.cpp: [intro.compliance] Implementation compliance:
 * diagnostic messages.
 *
 * [intro.compliance]/2: "If a program contains a violation of any diagnosable
 * rule ..., a conforming implementation shall issue at least one diagnostic
 * message."  [intro.compliance]/8: "Implementations are required to diagnose
 * programs that use such extensions that are ill-formed according to this
 * document."
 *
 * The messages are clang's, word for word (intro/diagnostic-messages.inc,
 * generated from its Diagnostic*.td), so that clang's tests and our harnesses
 * can compare them; how severe each one is follows clang's command line
 * (-W<group>, -Wno-<group>, -pedantic, -pedantic-errors, -Werror, -w).
 */
#include <stdio.h>

#include "../cplusplus-internal.h"

namespace cplusplus
{

namespace
{

enum class diagnostic_class : uint8_t {
    ERROR,
    WARNING,
    EXTENSION,
    EXTENSION_WARNING,
    NOTE,
    REMARK
};
enum class default_mapping : uint8_t { IGNORE, WARNING, ERROR, FATAL, NONE };

struct diagnostic_description {
    const char *name;
    diagnostic_class kind;
    default_mapping mapping;
    const char *group;
    const char *format;
};

constexpr diagnostic_description descriptions[] = {
#define DIAGNOSTIC(name, class, mapping, group, format)                        \
    {#name, diagnostic_class::class, default_mapping::mapping, group, format},
#include "diagnostic-messages.inc"
#include "../parse/diagnostic-messages.inc"
#include "../sema/diagnostic-messages.inc"
#undef DIAGNOSTIC
};

struct diagnostic_group_description {
    const char *name;
    const char *subgroups; /* separated by spaces */
};

constexpr diagnostic_group_description groups[] = {
#define DIAGNOSTIC_GROUP(name, subgroups) {name, subgroups},
#include "diagnostic-groups.inc"
#undef DIAGNOSTIC_GROUP
};

const diagnostic_group_description *find_group(const char *name, size_t length)
{
    for (const auto &g : groups)
        if (strlen(g.name) == length && !memcmp(g.name, name, length))
            return &g;
    return nullptr;
}

/* whether -W<setting> reaches group: the group itself or one of its
   subgroups, transitively (-Wall reaches -Wcomment through -Wmost) */
bool group_reaches(const char *setting, size_t length, const char *group,
                   unsigned depth)
{
    /* -Weverything: every warning group */
    if (length == 10 && !memcmp(setting, "everything", 10))
        return true;
    if (strlen(group) == length && !memcmp(setting, group, length))
        return true;
    const diagnostic_group_description *g = find_group(setting, length);
    if (!g || depth > 16)
        return false;
    for (const char *s = g->subgroups; *s;) {
        const char *e = strchr(s, ' ');
        size_t n = strlen(s);
        if (e)
            n = (size_t)(e - s);
        if (group_reaches(s, n, group, depth + 1))
            return true;
        s += n + (e != nullptr);
    }
    return false;
}

} // namespace

const char *diagnostic_name(diagnostic_id id)
{
    return descriptions[static_cast<int>(id)].name;
}
bool diagnostic_group_exists(const char *name)
{
    return find_group(name, strlen(name)) != nullptr;
}

const char *diagnostic_group(diagnostic_id id)
{
    return descriptions[static_cast<int>(id)].group;
}

bool diagnostic_options::parse(const char *argument)
{
    if (!strcmp(argument, "-pedantic") || !strcmp(argument, "-Wpedantic"))
        pedantic = true;
    else if (!strcmp(argument, "-pedantic-errors"))
        pedantic = pedantic_errors = true;
    else if (!strcmp(argument, "-Werror"))
        warnings_as_errors = true;
    else if (!strcmp(argument, "-w"))
        no_warnings = true;
    else if (!strncmp(argument, "-Werror=", 8) && group_setting_count < 32)
        group_settings[group_setting_count++] = {argument + 8, true, 1};
    else if (!strncmp(argument, "-Wno-error=", 11) && group_setting_count < 32)
        group_settings[group_setting_count++] = {argument + 11, true, -1};
    else if (!strncmp(argument, "-Wno-", 5) && group_setting_count < 32)
        group_settings[group_setting_count++] = {argument + 5, false, 0};
    else if (!strncmp(argument, "-W", 2) && argument[2] &&
             !strchr(argument, '=') && group_setting_count < 32)
        group_settings[group_setting_count++] = {argument + 2, true, 0};
    else
        return false;
    return true;
}

diagnostic_severity diagnostic_options::severity(diagnostic_id id) const
{
    const diagnostic_description &d = descriptions[static_cast<int>(id)];
    diagnostic_severity s;

    switch (d.mapping) {
    case default_mapping::IGNORE:
        s = diagnostic_severity::ignored;
        break;
    case default_mapping::WARNING:
        s = diagnostic_severity::warning;
        break;
    case default_mapping::ERROR:
        s = diagnostic_severity::error;
        break;
    case default_mapping::FATAL:
        s = diagnostic_severity::fatal;
        break;
    default:
        if (d.kind == diagnostic_class::REMARK)
            return diagnostic_severity::ignored;
        return diagnostic_severity::note;
    }
    if (d.kind == diagnostic_class::ERROR)
        return s;
    bool explicit_setting = false;
    int error = 0;
    if (*d.group)
        for (unsigned i = 0; i < group_setting_count; i++)
            if (group_reaches(group_settings[i].group,
                              strlen(group_settings[i].group), d.group, 0)) {
                if (group_settings[i].error) {
                    /* -Werror=x also enables x; -Wno-error=x only demotes */
                    error = group_settings[i].error;
                    if (error > 0 && s == diagnostic_severity::ignored)
                        s = diagnostic_severity::warning;
                    continue;
                }
                explicit_setting = true;
                if (!group_settings[i].enabled)
                    s = diagnostic_severity::ignored;
                else if (s == diagnostic_severity::ignored)
                    s = diagnostic_severity::warning;
            }
    /* -Wno-error=x: a warning that is an error by default is a warning */
    if (error < 0 && s == diagnostic_severity::error &&
        d.mapping == default_mapping::ERROR)
        s = diagnostic_severity::warning;
    if (!explicit_setting && (d.kind == diagnostic_class::EXTENSION ||
                              d.kind == diagnostic_class::EXTENSION_WARNING)) {
        if (pedantic_errors)
            s = diagnostic_severity::error;
        else if (pedantic && s == diagnostic_severity::ignored)
            s = diagnostic_severity::warning;
    }
    if (s == diagnostic_severity::warning && no_warnings)
        s = diagnostic_severity::ignored;
    if (s == diagnostic_severity::warning &&
        (error > 0 || (warnings_as_errors && error == 0)))
        s = diagnostic_severity::error;
    return s;
}

diagnostic_message &diagnostic_message::operator<<(int64_t value)
{
    if (argument_count < 4) {
        diagnostic_argument &a = arguments[argument_count++];
        a.is_integer = true;
        a.integer = value;
    }
    return *this;
}

diagnostic_message &diagnostic_message::text(const char *t, size_t length)
{
    if (argument_count < 4) {
        diagnostic_argument &a = arguments[argument_count++];
        a.is_integer = false;
        size_t kept = length;
        if (kept > sizeof a.text)
            kept = sizeof a.text;
        a.length = (uint8_t)kept;
        memcpy(a.text, t, a.length);
    }
    return *this;
}

diagnostic_message &diagnostic_message::operator<<(const char *t)
{
    return text(t, strlen(t));
}

bool diagnostic_consumer::map_group(const char *group,
                                    diagnostic_severity severity)
{
    if ((!find_group(group, strlen(group)) && strcmp(group, "everything")) ||
        strlen(group) >= 64)
        return false;
    group_mapping m{};
    memcpy(m.group, group, strlen(group));
    m.severity = severity;
    mappings.append(m);
    return true;
}

void diagnostic_consumer::push_mappings()
{
    mapping_pushes.append((uint32_t)mappings.size());
}

bool diagnostic_consumer::pop_mappings()
{
    if (mapping_pushes.empty())
        return false;
    mappings.resize(mapping_pushes.back());
    mapping_pushes.resize(mapping_pushes.size() - 1);
    return true;
}

diagnostic_message &diagnostic_consumer::issue(diagnostic_id id,
                                               source_location location)
{
    return issue_as(id, location, severity_now(id));
}

diagnostic_severity diagnostic_consumer::severity_now(diagnostic_id id) const
{
    diagnostic_severity severity = options.severity(id);
    /* the latest #pragma diagnostic that reaches the diagnostic's group */
    const diagnostic_description &d = descriptions[static_cast<int>(id)];
    if (*d.group && severity != diagnostic_severity::note)
        for (size_t i = mappings.size(); i-- > 0;)
            if (group_reaches(mappings[i].group, strlen(mappings[i].group),
                              d.group, 0)) {
                severity = mappings[i].severity;
                if (severity == diagnostic_severity::warning &&
                    options.no_warnings)
                    severity = diagnostic_severity::ignored;
                break;
            }
    return severity;
}

void diagnostic_consumer::discard_since(size_t count)
{
    for (size_t i = count; i < messages.size(); i++) {
        if (messages[i].severity >= diagnostic_severity::error)
            error_count--;
        if (messages[i].severity == diagnostic_severity::fatal)
            fatal_count--;
        else if (messages[i].severity == diagnostic_severity::warning)
            warning_count--;
    }
    messages.resize(count);
}

diagnostic_message &diagnostic_consumer::issue_as(diagnostic_id id,
                                                  source_location location,
                                                  diagnostic_severity severity)
{
    /* clang: after a fatal error no diagnostic is issued */
    if (suppressed || severity == diagnostic_severity::ignored || fatal_count) {
        discarded = diagnostic_message{};
        return discarded;
    }
    diagnostic_message m{};
    m.id = id;
    m.severity = severity;
    m.location = location + location_base;
    /* clang issues no warnings in system headers */
    if (severity == diagnostic_severity::warning && files) {
        const source_files::entry *e = files->find(m.location);
        if (e && e->system_header) {
            discarded = diagnostic_message{};
            return discarded;
        }
    }
    if (severity >= diagnostic_severity::error)
        error_count++;
    if (severity == diagnostic_severity::fatal)
        fatal_count++;
    else if (severity == diagnostic_severity::warning)
        warning_count++;
    return messages.append(m);
}

namespace
{

/* the n-th '|'-separated alternative of a %select{...} body */
void append_alternative(const char *body, size_t length, int64_t n,
                        vbuffer *out)
{
    size_t i = 0, start = 0;
    int depth = 0;
    int64_t k = 0;
    for (; i <= length; i++) {
        if (i < length && body[i] == '{')
            depth++;
        else if (i < length && body[i] == '}')
            depth--;
        else if (i == length || (body[i] == '|' && depth == 0)) {
            if (k++ == n) {
                vbuffer_append(out, body + start, i - start);
                return;
            }
            start = i + 1;
        }
    }
}

void append_argument(const diagnostic_argument &a, vbuffer *out)
{
    if (a.is_integer)
        vbuffer_format_append(out, "%lld", (long long)a.integer);
    else
        vbuffer_append(out, a.text, a.length);
}

} // namespace

/* clang's format language, the part the lexer's messages use: %N, %select,
   %sN (an 's' unless the argument is 1), %plural{1:a|:b}N */
/* the format [f, end) with the arguments substituted; an alternative of a
   %select is itself a format (%1 inside %select{in %1|...}0) */
static void format_into(const diagnostic_message &message, const char *f,
                        const char *end, vbuffer *out)
{
    while (f < end && *f) {
        if (*f != '%') {
            vbuffer_append(out, f++, 1);
            continue;
        }
        f++;
        if (*f == '%') {
            vbuffer_append(out, f++, 1);
            continue;
        }
        const char *name = f;
        while ((*f >= 'a' && *f <= 'z') || (*f >= 'A' && *f <= 'Z'))
            f++;
        size_t name_length = (size_t)(f - name);
        const char *body = nullptr;
        size_t body_length = 0;
        if (*f == '{') {
            int depth = 1;
            body = ++f;
            while (*f && depth) {
                if (*f == '{')
                    depth++;
                else if (*f == '}')
                    depth--;
                f++;
            }
            body_length = (size_t)(f - body - 1);
        }
        unsigned index = 0;
        while (*f >= '0' && *f <= '9')
            index = index * 10 + (unsigned)(*f++ - '0');
        if (index >= message.argument_count)
            continue;
        const diagnostic_argument &a = message.arguments[index];
        if (name_length == 6 && !memcmp(name, "select", 6)) {
            owned_buffer alternative;
            append_alternative(body, body_length, a.integer, alternative.get());
            format_into(message, alternative.data(),
                        alternative.data() + alternative.size(), out);
        } else if (name_length == 1 && *name == 's') {
            if (a.integer != 1)
                vbuffer_append(out, "s", 1);
        } else if (name_length == 6 && !memcmp(name, "plural", 6)) {
            /* {1:one|:other} */
            const char *colon = (const char *)memchr(body, ':', body_length);
            bool one =
                colon && colon == body + 1 && body[0] == '1' && a.integer == 1;
            const char *bar = (const char *)memchr(body, '|', body_length);
            if (one && bar)
                vbuffer_append(out, colon + 1, (size_t)(bar - colon - 1));
            else if (bar)
                vbuffer_append(out, bar + 2,
                               (size_t)(body + body_length - bar - 2));
        } else
            append_argument(a, out);
    }
}

void diagnostic_text(const diagnostic_message &message, vbuffer *out)
{
    const char *f = descriptions[static_cast<int>(message.id)].format;
    format_into(message, f, f + strlen(f), out);
}

void diagnostic_consumer::render(const diagnostic_message &message,
                                 vbuffer *out) const
{
    static constexpr const char *severities[] = {
        "ignored", "note", "remark", "warning", "error", "fatal error"};
    source_position p;
    if (files)
        p = files->position(message.location);
    else
        p = file->position(message.location);
    vbuffer_format_append(out, "%s:%u:%u: %s: ", p.file_name, p.line, p.column,
                          severities[static_cast<int>(message.severity)]);
    diagnostic_text(message, out);
    const char *group = diagnostic_group(message.id);
    if (*group)
        vbuffer_format_append(out, " [-W%s]", group);
    vbuffer_append(out, "\n", 1);
}

} // namespace cplusplus
