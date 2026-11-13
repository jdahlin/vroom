/* Digests used by the linkers and the header cache (digest-xxh3.c, digest-blake3.c, digest-sha256.c): XXH3-64 and BLAKE3 (lld's), SHA-256 (Mach-O code signatures). */
#ifndef VCC_COMMON_DIGEST_H
#define VCC_COMMON_DIGEST_H
#include <stddef.h>
#include <stdint.h>

__attribute__((warn_unused_result)) uint64_t vdigest_xxh3(const void *data, size_t len);
void vdigest_blake3(const void *data, size_t len, uint8_t *out, size_t outlen);
void vdigest_sha256(const void *data, size_t len, uint8_t out[32]);

#endif
