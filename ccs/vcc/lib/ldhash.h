/* Hashes used by the linker to match lld: XXH3-64 and BLAKE3. */
#ifndef VCC_LDHASH_H
#define VCC_LDHASH_H
#include <stddef.h>
#include <stdint.h>

__attribute__((warn_unused_result)) uint64_t ld_xxh3(const void *data, size_t len);
void ld_blake3(const void *data, size_t len, uint8_t *out, size_t outlen);

#endif
