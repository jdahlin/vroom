#ifndef VCC_COMMON_CMDLIB_H
#define VCC_COMMON_CMDLIB_H

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Exact spellings preserve driver dialects such as "-gdwarf-4". A metavar
 * denotes one operand, either attached to the spelling or in the next argv. */
typedef struct cmdlib_option {
    int id;
    const char *spelling, *metavar, *description;
    bool attached;
} cmdlib_option;

static inline int cmdlib_match(const cmdlib_option *options, int argc,
                               char **argv, const char **value)
{
    if (value)
        *value = NULL;
    if (argc <= 0)
        return 0;
    for (const cmdlib_option *o = options; o->spelling; o++) {
        if (o->attached) {
            size_t n = strlen(o->spelling);
            if (strncmp(argv[0], o->spelling, n))
                continue;
            if (value)
                *value = argv[0] + n;
        } else {
            if (strcmp(argv[0], o->spelling) || (o->metavar && argc < 2))
                continue;
            if (value && o->metavar)
                *value = argv[1];
        }
        return o->id;
    }
    return 0;
}

static inline void cmdlib_print_help(const char *usage,
                                     const cmdlib_option *options)
{
    puts(usage);
    puts("  --help\n      Show available options.");
    for (const cmdlib_option *o = options; o->spelling; o++)
        fprintf(stdout, "  %s%s%s\n      %s\n", o->spelling,
                o->metavar && !o->attached ? " " : "",
                o->metavar ? o->metavar : "", o->description);
}

/* Return the operand of NAME=VALUE or NAME VALUE, advancing only for the latter. */
static inline const char *cmdlib_value(const char *name, int argc, char **argv,
                                       int *i)
{
    const char *arg = argv[*i];
    size_t n = strlen(name);
    if (strncmp(arg, name, n))
        return NULL;
    if (arg[n] == '=')
        return arg + n + 1;
    if (!arg[n] && *i + 1 < argc)
        return argv[++*i];
    return NULL;
}

/* Forwarding drivers handle their own help only when it is the entire request. */
static inline bool cmdlib_wants_help(int argc, char **argv)
{
    return argc == 2 && !strcmp(argv[1], "--help");
}

#endif
