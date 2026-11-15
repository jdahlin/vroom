#ifndef VCC_COMMON_CLI_OPTIONS_H
#define VCC_COMMON_CLI_OPTIONS_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Exact spellings preserve driver dialects such as "-gdwarf-4". A metavar
 * denotes one operand, either attached to the spelling or in the next argv. */
typedef struct vcli_option {
    const char *spelling, *metavar, *description;
    bool attached;
} vcli_option;

static inline const vcli_option *vcli_match(const vcli_option *options,
                                            int argc, char **argv,
                                            const char **value)
{
    *value = NULL;
    if (argc <= 0)
        return NULL;
    for (const vcli_option *o = options; o->spelling; o++) {
        if (o->attached) {
            size_t n = strlen(o->spelling);
            if (strncmp(argv[0], o->spelling, n))
                continue;
            *value = argv[0] + n;
        } else {
            if (strcmp(argv[0], o->spelling) || (o->metavar && argc < 2))
                continue;
            if (o->metavar)
                *value = argv[1];
        }
        return o;
    }
    return NULL;
}

static inline void vcli_print_options(FILE *out, const vcli_option *options)
{
    for (const vcli_option *o = options; o->spelling; o++)
        fprintf(out, "  %s%s%s\n      %s\n", o->spelling,
                o->metavar && !o->attached ? " " : "",
                o->metavar ? o->metavar : "", o->description);
}

#endif
