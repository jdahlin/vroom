/* shared bits of the vcc fuzz targets */
#ifndef VCC_FUZZ_H
#define VCC_FUZZ_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);
int LLVMFuzzerInitialize(int *argc, char ***argv);

/* an invariant failed: say which and crash (the fuzzer saves the input) */
#define FUZZ_CHECK(c, ...) do {						\
	if (__builtin_expect(!(c), 0)) {				\
		fprintf(stderr, "fuzz check failed: %s (%s:%d): ", #c, __FILE__, __LINE__); \
		fprintf(stderr, __VA_ARGS__);				\
		fputc('\n', stderr);					\
		abort();						\
	}								\
} while (0)

#endif
