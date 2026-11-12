/* Hashes used by the linkers: XXH3-64 and BLAKE3 (lld's), SHA-256 (Mach-O code signatures). */
#ifndef VCC_LDHASH_H
#define VCC_LDHASH_H
#include <stddef.h>
#include <stdint.h>

__attribute__((warn_unused_result)) uint64_t ld_xxh3(const void *data, size_t len);
void ld_blake3(const void *data, size_t len, uint8_t *out, size_t outlen);
void ld_sha256(const void *data, size_t len, uint8_t out[32]);

#endif
