/*
 * vcc's <arm_acle.h>: the CRC32 intrinsics, as inline asm; on other targets
 * (vcc built for WebAssembly by itself, W6) in C with the same values.
 */
#ifndef __VCC_ARM_ACLE_H
#define __VCC_ARM_ACLE_H

#include <stdint.h>

#ifdef __aarch64__
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
#else
/* reflected, no pre- or post-inversion, as the instructions: a nibble at a time */
static inline __attribute__((always_inline)) uint32_t __vcc_crc(uint32_t c, uint64_t v, int n, int k)
{
	static const uint32_t t[2][16] = {
		{ 0x00000000, 0x1db71064, 0x3b6e20c8, 0x26d930ac, 0x76dc4190, 0x6b6b51f4, 0x4db26158, 0x5005713c,
		  0xedb88320, 0xf00f9344, 0xd6d6a3e8, 0xcb61b38c, 0x9b64c2b0, 0x86d3d2d4, 0xa00ae278, 0xbdbdf21c },
		{ 0x00000000, 0x105ec76f, 0x20bd8ede, 0x30e349b1, 0x417b1dbc, 0x5125dad3, 0x61c69362, 0x7198540d,
		  0x82f63b78, 0x92a8fc17, 0xa24bb5a6, 0xb21572c9, 0xc38d26c4, 0xd3d3e1ab, 0xe330a81a, 0xf36e6f75 },
	};
	for (int i = 0; i < n; i++, v >>= 8) {
		c ^= (uint32_t)(v & 0xff);
		c = (c >> 4) ^ t[k][c & 15];
		c = (c >> 4) ^ t[k][c & 15];
	}
	return c;
}
#define __VCC_CRC(name, T, k) \
static inline __attribute__((always_inline)) uint32_t name(uint32_t a, T b) { return __vcc_crc(a, b, sizeof(T), k); }
__VCC_CRC(__crc32b, uint8_t, 0)
__VCC_CRC(__crc32h, uint16_t, 0)
__VCC_CRC(__crc32w, uint32_t, 0)
__VCC_CRC(__crc32d, uint64_t, 0)
__VCC_CRC(__crc32cb, uint8_t, 1)
__VCC_CRC(__crc32ch, uint16_t, 1)
__VCC_CRC(__crc32cw, uint32_t, 1)
__VCC_CRC(__crc32cd, uint64_t, 1)
#undef __VCC_CRC
#endif

#endif
