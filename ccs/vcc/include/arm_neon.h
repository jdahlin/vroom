/*
 * vcc's <arm_neon.h> (Darwin target): the intrinsics vcc's own sources use.
 *
 * vcc has no vector types: a vector is an aligned struct of its bytes, and
 * each intrinsic loads its operands into q16-q20, runs the instruction and
 * stores the result (inline asm on memory operands; .inst where vas lacks
 * the mnemonic).  Same results as
 * clang's header, not its speed; layout and size match, the calling
 * convention does not (a vector is passed like a struct), so vectors must
 * not cross into code compiled by another compiler.
 */
#ifndef __VCC_ARM_NEON_H
#define __VCC_ARM_NEON_H

#include <stdint.h>

typedef struct { uint8_t __b[16]; } __attribute__((aligned(16))) uint8x16_t;
typedef struct { uint8_t __b[16]; } __attribute__((aligned(16))) int8x16_t;
typedef struct { uint8_t __b[16]; } __attribute__((aligned(16))) uint16x8_t;
typedef struct { uint8_t __b[16]; } __attribute__((aligned(16))) uint32x4_t;
typedef struct { uint8_t __b[16]; } __attribute__((aligned(16))) uint64x2_t;
typedef struct { uint8_t __b[8]; } __attribute__((aligned(8))) uint8x8_t;
typedef struct { uint8_t __b[8]; } __attribute__((aligned(8))) uint64x1_t;
typedef struct { uint8x16_t val[4]; } uint8x16x4_t;

#define __vai static inline __attribute__((always_inline))

/* q16 = a, q17 = b; insn; result from q16 */
#define __VCC_OP1(name, T, R, insn, st)							\
__vai R name(T a) { R r; __asm__("ldr q16, [%1]\n\t" insn "\n\t" st " [%0]"		\
	: : "r"(&r), "r"(&a) : "v16", "memory"); return r; }
#define __VCC_OP2(name, T, insn)							\
__vai T name(T a, T b) { T r; __asm__("ldr q16, [%1]\n\tldr q17, [%2]\n\t" insn "\n\tstr q16, [%0]" \
	: : "r"(&r), "r"(&a), "r"(&b) : "v16", "v17", "memory"); return r; }
#define __VCC_SH(name, T, R, insn, st)							\
__vai R name(T a, const int n) { R r; __asm__("ldr q16, [%1]\n\t" insn ", #%2\n\t" st " [%0]" \
	: : "r"(&r), "r"(&a), "i"(n) : "v16", "memory"); return r; }

__VCC_OP2(vandq_u8, uint8x16_t, "and v16.16b, v16.16b, v17.16b")
__VCC_OP2(vorrq_u8, uint8x16_t, "orr v16.16b, v16.16b, v17.16b")
__VCC_OP2(vsubq_u8, uint8x16_t, "sub v16.16b, v16.16b, v17.16b")
__VCC_OP2(vceqq_u8, uint8x16_t, ".inst 0x6e318e10 /* cmeq v16.16b, v16.16b, v17.16b */")
__VCC_OP2(vtstq_u8, uint8x16_t, ".inst 0x4e318e10 /* cmtst v16.16b, v16.16b, v17.16b */")
__VCC_OP2(vcleq_u8, uint8x16_t, ".inst 0x6e303e30 /* cmhs v16.16b, v17.16b, v16.16b */")
__VCC_OP2(vqtbl1q_u8, uint8x16_t, ".inst 0x4e110210 /* tbl v16.16b, {v16.16b}, v17.16b */")
__VCC_OP1(vceqzq_u8, uint8x16_t, uint8x16_t, ".inst 0x4e209a10 /* cmeq v16.16b, v16.16b, #0 */", "str q16,")
__VCC_SH(vshrq_n_u8, uint8x16_t, uint8x16_t, "ushr v16.16b, v16.16b", "str q16,")
__VCC_SH(vshrq_n_s8, int8x16_t, int8x16_t, "sshr v16.16b, v16.16b", "str q16,")
__vai uint8x8_t vshrn_n_u16(uint16x8_t a, const int n)
{
	uint8x8_t r;
	__asm__("ldr q16, [%1]\n\t.inst 0x0f008610 | ((16 - %2) << 16) /* shrn v16.8b, v16.8h, #n */\n\tstr d16, [%0]"
		: : "r"(&r), "r"(&a), "i"(n) : "v16", "memory");
	return r;
}

__vai uint8x16_t vsriq_n_u8(uint8x16_t a, uint8x16_t b, const int n)
{
	uint8x16_t r;
	__asm__("ldr q16, [%1]\n\tldr q17, [%2]\n\tsri v16.16b, v17.16b, #%3\n\tstr q16, [%0]"
		: : "r"(&r), "r"(&a), "r"(&b), "i"(n) : "v16", "v17", "memory");
	return r;
}

__vai uint8x16_t vextq_u8(uint8x16_t a, uint8x16_t b, const int n)
{
	uint8x16_t r;
	__asm__("ldr q16, [%1]\n\tldr q17, [%2]\n\text v16.16b, v16.16b, v17.16b, #%3\n\tstr q16, [%0]"
		: : "r"(&r), "r"(&a), "r"(&b), "i"(n) : "v16", "v17", "memory");
	return r;
}

__vai uint8x16_t vqtbl4q_u8(uint8x16x4_t t, uint8x16_t idx)
{
	uint8x16_t r;
	__asm__("ld1 {v16.16b, v17.16b, v18.16b, v19.16b}, [%1]\n\tldr q20, [%2]\n\t"
		".inst 0x4e146210 /* tbl v16.16b, {v16-v19.16b}, v20.16b */\n\tstr q16, [%0]"
		: : "r"(&r), "r"(&t), "r"(&idx) : "v16", "v17", "v18", "v19", "v20", "memory");
	return r;
}

__vai uint8x16_t vdupq_n_u8(uint8_t x)
{
	uint8x16_t r;
	__asm__("dup v16.16b, %w1\n\tstr q16, [%0]" : : "r"(&r), "r"(x) : "v16", "memory");
	return r;
}

__vai uint8x16_t vld1q_u8(const uint8_t *p) { uint8x16_t r; __builtin_memcpy(&r, p, 16); return r; }
__vai uint8x16_t vld1q_dup_u8(const uint8_t *p) { return vdupq_n_u8(*p); }
__vai uint8x16x4_t vld1q_u8_x4(const uint8_t *p) { uint8x16x4_t r; __builtin_memcpy(&r, p, 64); return r; }

__vai uint8x16x4_t vld4q_u8(const uint8_t *p)
{
	uint8x16x4_t r;
	__asm__("mov x17, %1\n\t.inst 0x4c400230 /* ld4 {v16-v19.16b}, [x17] */\n\t"
		"st1 {v16.16b, v17.16b, v18.16b, v19.16b}, [%0]"
		: : "r"(&r), "r"(p) : "x17", "v16", "v17", "v18", "v19", "memory");
	return r;
}

__vai uint64_t vget_lane_u64(uint64x1_t v, const int lane) { uint64_t x; (void)lane; __builtin_memcpy(&x, &v, 8); return x; }

#define __VCC_CAST(name, F, T) __vai T name(F v) { T r; __builtin_memcpy(&r, &v, sizeof r); return r; }
__VCC_CAST(vreinterpret_u64_u8, uint8x8_t, uint64x1_t)
__VCC_CAST(vreinterpretq_s8_u8, uint8x16_t, int8x16_t)
__VCC_CAST(vreinterpretq_u8_s8, int8x16_t, uint8x16_t)
__VCC_CAST(vreinterpretq_u16_u8, uint8x16_t, uint16x8_t)
__VCC_CAST(vreinterpretq_u8_u16, uint16x8_t, uint8x16_t)

#undef __VCC_OP1
#undef __VCC_OP2
#undef __VCC_SH
#undef __VCC_CAST
#undef __vai
#endif
