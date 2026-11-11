/* CRC32C for string hashes on the host: ACLE on arm64, SSE4.2 on x86-64 (the
 * same polynomial, so the same hashes); header-only */
#ifndef VCC_LIB_HOSTCRC_H
#define VCC_LIB_HOSTCRC_H
#if defined(__x86_64__)
#include <nmmintrin.h>
#include <stdint.h>
#define __crc32cd(h, w) ((uint32_t)_mm_crc32_u64((h), (w)))
#define __crc32cb(h, b) _mm_crc32_u8((h), (b))
#else
#include <arm_acle.h>
#endif
#endif
