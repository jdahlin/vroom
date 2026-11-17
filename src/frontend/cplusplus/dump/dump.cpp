/*
 * dump.cpp: -fcxx-dump=WHAT[,WHAT...], the developer outputs of dump.h:
 * their names, the phase each needs, and the selftest.
 */
#include <string.h>

#include "../cplusplus-internal.h"
#include "dump.h"

namespace cplusplus
{

namespace
{

struct dump_description {
    const char *name;
    dump_phase phase;
    const char *what;
};

constexpr dump_description dump_descriptions[] = {
#define X(identifier, name, phase, what) {name, dump_phase::phase, what},
    CPLUSPLUS_DUMPS(X)
#undef X
};

constexpr size_t dump_description_count =
    sizeof dump_descriptions / sizeof *dump_descriptions;

} // namespace

dump_phase dump_request::phase() const
{
    dump_phase deepest = dump_phase::none;
    for (size_t i = 0; i < dump_description_count; i++)
        if ((outputs & (1u << i)) && dump_descriptions[i].phase > deepest)
            deepest = dump_descriptions[i].phase;
    return deepest;
}

bool dump_request_parse(const char *list, dump_request &r, const char **bad)
{
    const char *s = list;
    for (;;) {
        const char *comma = strchr(s, ',');
        size_t n = strlen(s);
        if (comma)
            n = size_t(comma - s);
        bool found = false;
        for (size_t i = 0; i < dump_description_count; i++)
            if (strlen(dump_descriptions[i].name) == n &&
                !memcmp(dump_descriptions[i].name, s, n)) {
                r.outputs |= 1u << i;
                found = true;
            }
        if (!found) {
            *bad = s;
            return false;
        }
        if (!comma)
            return true;
        s = comma + 1;
    }
}

void dump_help(vbuffer *out)
{
    vbuffer_string_append(
        out, "vcc -x c++ -fsyntax-only -fcxx-dump=WHAT[,WHAT...] FILE: the C++ "
             "front end's developer outputs on stdout, in this order\n"
             "(-fcxx-phase4-diagnostics-only: no phase 7 diagnostics; "
             "-fcxx-debug-fallbacks: the parser reports what it keeps as "
             "tokens)\n");
    for (size_t i = 0; i < dump_description_count; i++)
        vbuffer_format_append(out, "  %-20s %s\n", dump_descriptions[i].name,
                              dump_descriptions[i].what);
}

bool dump_selftest(vbuffer *out)
{
    bool parsing = dump_parsing_selftest(out);
    bool analysis = dump_analysis_selftest(out);
    if (parsing && analysis)
        vbuffer_string_append(out, "selftest: ok\n");
    return parsing && analysis;
}

} // namespace cplusplus
