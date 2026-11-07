#ifndef VCC_CAPS_H
#define VCC_CAPS_H

/*
 * Returns how many argv entries the option consumes (1 or 2), or 0 if vcc
 * does not support it; *why is set to the offending option.
 */
int cc_option(const char *a, const char *next, const char **why);
/* 1 if vld supports the option, 0 otherwise */
int ld_option(const char *a, const char **why);

#include <stdbool.h>
/* true if vcc predefines the macro named in "NAME[=VALUE]" */
bool cc_predefined(const char *def);

#endif
