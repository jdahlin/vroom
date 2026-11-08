/* vcc's <arm_acle.h>: the CRC32 intrinsics, as inline asm. */
#ifndef __VCC_ARM_ACLE_H
#define __VCC_ARM_ACLE_H

#include <stdint.h>

#define __VCC_CRC(name, insn, T, w)						\
static inline __attribute__((always_inline)) uint32_t name(uint32_t a, T b)	\
{ uint32_t r; __asm__(insn " %w0, %w1, %" w "2" : "=r"(r) : "r"(a), "r"(b)); return r; }

__VCC_CRC(__crc32b, "crc32b", uint8_t, "w")
__VCC_CRC(__crc32h, "crc32h", uint16_t, "w")
__VCC_CRC(__crc32w, "crc32w", uint32_t, "w")
__VCC_CRC(__crc32d, "crc32x", uint64_t, "x")
__VCC_CRC(__crc32cb, "crc32cb", uint8_t, "w")
__VCC_CRC(__crc32ch, "crc32ch", uint16_t, "w")
__VCC_CRC(__crc32cw, "crc32cw", uint32_t, "w")
__VCC_CRC(__crc32cd, "crc32cx", uint64_t, "x")
#undef __VCC_CRC

#endif
