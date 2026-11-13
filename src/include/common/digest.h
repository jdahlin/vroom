/*
 * The digests the tools need, one file each (digest-xxh3.c, digest-blake3.c,
 * digest-sha256.c): no dependency, the same bytes as the reference.
 *
 *   vdigest_xxh3(data, len)                  XXH3-64, seed 0 (lld's merge-section sharding; the header cache key)
 *   vdigest_blake3(data, len, out, outlen)   BLAKE3 truncated to outlen bytes (lld's --build-id=sha1 and md5)
 *   vdigest_sha256(data, len, out)           SHA-256 (Mach-O code signatures)
 *
 * They are not the tables' string hash: that is hash.h.
 */
#ifndef VCC_COMMON_DIGEST_H
#define VCC_COMMON_DIGEST_H
#include <stddef.h>
#include <stdint.h>

__attribute__((warn_unused_result)) uint64_t vdigest_xxh3(const void *data, size_t len);
void vdigest_blake3(const void *data, size_t len, uint8_t *out, size_t outlen);
void vdigest_sha256(const void *data, size_t len, uint8_t out[32]);

#endif
