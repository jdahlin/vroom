/* The checks of the common/ and port/ unit tests: CHECK(cond) counts and reports a failure; the test's main returns check_failures. */
#ifndef VCC_TEST_COMMON_CHECK_H
#define VCC_TEST_COMMON_CHECK_H

#include <stdio.h>

static int check_failures;
#define CHECK(cond) do { \
	if (!(cond)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
		check_failures++; \
	} \
} while (0)

#endif
