/*
 * vcc's arm_neon.h: the AdvSIMD types and the intrinsics the kernel uses,
 * written as GNU vector types plus inline asm.  Each intrinsic is a macro
 * around a statement expression so immediates stay constant expressions.
 */
#ifndef __VCC_ARM_NEON_H
#define __VCC_ARM_NEON_H

#include <stdint.h>


#define __vv(t, n) __attribute__((__vector_size__(n))) t
typedef __vv(signed char, 8) int8x8_t;	typedef __vv(signed char, 16) int8x16_t;
typedef __vv(unsigned char, 8) uint8x8_t;	typedef __vv(unsigned char, 16) uint8x16_t;
typedef __vv(short, 8) int16x4_t;	typedef __vv(short, 16) int16x8_t;
typedef __vv(unsigned short, 8) uint16x4_t;	typedef __vv(unsigned short, 16) uint16x8_t;
typedef __vv(int, 8) int32x2_t;	typedef __vv(int, 16) int32x4_t;
typedef __vv(unsigned int, 8) uint32x2_t;	typedef __vv(unsigned int, 16) uint32x4_t;
typedef __vv(long long, 8) int64x1_t;	typedef __vv(long long, 16) int64x2_t;
typedef __vv(unsigned long long, 8) uint64x1_t;	typedef __vv(unsigned long long, 16) uint64x2_t;
typedef __vv(unsigned char, 8) poly8x8_t;	typedef __vv(unsigned char, 16) poly8x16_t;
typedef __vv(unsigned long long, 8) poly64x1_t;	typedef __vv(unsigned long long, 16) poly64x2_t;
typedef unsigned char poly8_t;
typedef unsigned long long poly64_t;
typedef unsigned __int128 poly128_t;
#undef __vv
typedef struct uint8x8x2_t { uint8x8_t val[2]; } uint8x8x2_t;
typedef struct uint8x16x2_t { uint8x16_t val[2]; } uint8x16x2_t;
typedef struct uint8x16x4_t { uint8x16_t val[4]; } uint8x16x4_t;

/* loads and stores */
#define __vcc_ld(T, p) ({ T __r; __builtin_memcpy(&__r, (p), sizeof __r); __r; })
#define __vcc_st(T, p, v) ({ T __v = (v); __builtin_memcpy((p), &__v, sizeof __v); (void)0; })
#define vld1q_u8(p) __vcc_ld(uint8x16_t, (const unsigned char *)(p))
#define vld1q_u64(p) __vcc_ld(uint64x2_t, (const unsigned long long *)(p))
#define vld1q_u8_x4(p) __vcc_ld(uint8x16x4_t, (const unsigned char *)(p))
#define vld1_u8(p) __vcc_ld(uint8x8_t, (const unsigned char *)(p))
#define vst1q_u8(p, v) __vcc_st(uint8x16_t, (unsigned char *)(p), v)
#define vst1q_u64(p, v) __vcc_st(uint64x2_t, (unsigned long long *)(p), v)
#define vst1_u8(p, v) __vcc_st(uint8x8_t, (unsigned char *)(p), v)

/* bitwise */
#define veorq_u8(a, b) ((uint8x16_t)(a) ^ (uint8x16_t)(b))
#define veorq_u64(a, b) ((uint64x2_t)(a) ^ (uint64x2_t)(b))
#define vandq_u8(a, b) ((uint8x16_t)(a) & (uint8x16_t)(b))
#define vandq_u64(a, b) ((uint64x2_t)(a) & (uint64x2_t)(b))
#define vorrq_u8(a, b) ((uint8x16_t)(a) | (uint8x16_t)(b))
#define vorrq_u64(a, b) ((uint64x2_t)(a) | (uint64x2_t)(b))

/* lanes and moves */
#define vdupq_n_u8(x) ({ uint8x16_t __r; asm("dup %0.16b, %w1" : "=w"(__r) : "r"((unsigned int)(unsigned char)(x))); __r; })
#define vdupq_n_u64(x) ({ uint64x2_t __r; asm("dup %0.2d, %x1" : "=w"(__r) : "r"((unsigned long long)(x))); __r; })
#define vgetq_lane_u64(v, i) ({ uint64x2_t __v = (v); __v[(i) & 1]; })
#define vgetq_lane_u8(v, i) ({ uint8x16_t __v = (v); __v[(i) & 15]; })
#define vget_low_u8(v) ({ uint8x16_t __v = (v); uint8x8_t __r; __builtin_memcpy(&__r, &__v, 8); __r; })
#define vget_high_u8(v) ({ uint8x16_t __v = (v); uint8x8_t __r; __builtin_memcpy(&__r, (char *)&__v + 8, 8); __r; })
#define vcombine_u8(l, h) ({ uint8x8_t __l = (l), __h = (h); uint8x16_t __r; __builtin_memcpy(&__r, &__l, 8); \
	__builtin_memcpy((char *)&__r + 8, &__h, 8); __r; })
#define vextq_u8(a, b, n) ({ uint8x16_t __r, __a = (a), __b = (b); \
	asm("ext %0.16b, %1.16b, %2.16b, %3" : "=w"(__r) : "w"(__a), "w"(__b), "i"(n)); __r; })
#define vextq_u64(a, b, n) ({ uint64x2_t __r, __a = (a), __b = (b); \
	asm("ext %0.16b, %1.16b, %2.16b, %3" : "=w"(__r) : "w"(__a), "w"(__b), "i"(8 * (n))); __r; })

/* shifts */
#define vshrq_n_u8(a, n) ({ uint8x16_t __r, __a = (a); asm("ushr %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshrq_n_s8(a, n) ({ int8x16_t __r, __a = (a); asm("sshr %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshlq_n_u8(a, n) ({ uint8x16_t __r, __a = (a); asm("shl %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshrq_n_u64(a, n) ({ uint64x2_t __r, __a = (a); asm("ushr %0.2d, %1.2d, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshlq_n_u64(a, n) ({ uint64x2_t __r, __a = (a); asm("shl %0.2d, %1.2d, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })

/* table lookups and polynomial multiplies */
#define vqtbl1q_u8(t, i) ({ uint8x16_t __r, __t = (t), __i = (i); \
	asm("tbl %0.16b, {%1.16b}, %2.16b" : "=w"(__r) : "w"(__t), "w"(__i)); __r; })
#define vqtbl4q_u8(t, i) ({ uint8x16x4_t __t = (t); uint8x16_t __i = (i), __r; const unsigned char *__b = (const unsigned char *)&__t; \
	for (int __k = 0; __k < 16; __k++) __r[__k] = __i[__k] < 64 ? __b[__i[__k]] : 0; __r; })
#define vtbl2_u8(t, i) ({ uint8x8x2_t __t = (t); uint8x8_t __i = (i), __r; uint8x16_t __q = vcombine_u8(__t.val[0], __t.val[1]); \
	asm("tbl %0.8b, {%1.16b}, %2.8b" : "=w"(__r) : "w"(__q), "w"(__i)); __r; })
#define vmulq_p8(a, b) ({ poly8x16_t __r, __a = (a), __b = (b); \
	asm("pmul %0.16b, %1.16b, %2.16b" : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vmull_p64(a, b) ({ uint64x2_t __a = { (a), 0 }, __b = { (b), 0 }, __r; \
	asm(".arch_extension aes\n\tpmull %0.1q, %1.1d, %2.1d" : "=w"(__r) : "w"(__a), "w"(__b)); \
	poly128_t __p; __builtin_memcpy(&__p, &__r, 16); __p; })
#define vmull_high_p64(a, b) ({ poly64x2_t __a = (a), __b = (b); uint64x2_t __r; \
	asm(".arch_extension aes\n\tpmull2 %0.1q, %1.2d, %2.2d" : "=w"(__r) : "w"(__a), "w"(__b)); \
	poly128_t __p; __builtin_memcpy(&__p, &__r, 16); __p; })

/* reinterpretations */
#define vreinterpretq_u64_u8(x) ((uint64x2_t)(x))
#define vreinterpretq_u16_u8(x) ((uint16x8_t)(x))
#define vreinterpret_u64_u8(x) ((uint64x1_t)(x))
#define vget_lane_u64(v, i) ({ uint64x1_t __v = (v); (void)(i); __v[0]; })
#define vshrn_n_u16(a, n) ({ uint16x8_t __a = (a); uint8x8_t __r; \
	for (int __k = 0; __k < 8; __k++) __r[__k] = (unsigned char)(__a[__k] >> (n)); __r; })
#define vsriq_n_u8(a, b, n) ({ uint8x16_t __r = (a), __b = (b); asm("sri %0.16b, %1.16b, %2" : "+w"(__r) : "w"(__b), "i"(n)); __r; })
#define vreinterpretq_u8_u64(x) ((uint8x16_t)(x))
#define vreinterpretq_p64_u64(x) ((poly64x2_t)(x))
#define vreinterpretq_u64_p64(x) ((uint64x2_t)(x))
#define vreinterpretq_u64_p128(x) ({ poly128_t __x = (x); uint64x2_t __r; __builtin_memcpy(&__r, &__x, 16); __r; })
#define vreinterpretq_p128_u64(x) ({ uint64x2_t __x = (x); poly128_t __r; __builtin_memcpy(&__r, &__x, 16); __r; })


/* used by vcc's own lexer */
#define vreinterpretq_s8_u8(x) ((int8x16_t)(x))
#define vreinterpretq_u8_s8(x) ((uint8x16_t)(x))
#define vsubq_u8(a, b) ((uint8x16_t)(a) - (uint8x16_t)(b))
#define vld1q_dup_u8(p) vdupq_n_u8(*(const unsigned char *)(p))
#define __VCC_CMP(op, a, b) ({ uint8x16_t __r, __a = (a), __b = (b); \
	asm(op " %0.16b, %1.16b, %2.16b" : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vceqq_u8(a, b) __VCC_CMP("cmeq", a, b)
#define vtstq_u8(a, b) __VCC_CMP("cmtst", a, b)
#define vcleq_u8(a, b) __VCC_CMP("cmhs", b, a)
#define vceqzq_u8(a) vceqq_u8(a, vdupq_n_u8(0))
#define vld4q_u8(p) ({ const unsigned char *__p = (const unsigned char *)(p); uint8x16x4_t __r; \
	for (int __k = 0; __k < 64; __k++) __r.val[__k & 3][__k >> 2] = __p[__k]; __r; })

/* used by PostgreSQL (port/simd.h, pg_popcount_aarch64.c) */
#define vld1q_u32(p) __vcc_ld(uint32x4_t, (const unsigned int *)(p))
#define vdupq_n_u32(x) ({ uint32x4_t __r; asm("dup %0.4s, %w1" : "=w"(__r) : "r"((unsigned int)(x))); __r; })
#define vorrq_u32(a, b) ((uint32x4_t)(a) | (uint32x4_t)(b))
#define vaddq_u64(a, b) ((uint64x2_t)(a) + (uint64x2_t)(b))
#define __VCC_V3(T, op, arr, a, b) ({ T __r, __a = (a), __b = (b); \
	asm(op " %0." arr ", %1." arr ", %2." arr : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vqsubq_u8(a, b) __VCC_V3(uint8x16_t, "uqsub", "16b", a, b)
#define vminq_u8(a, b) __VCC_V3(uint8x16_t, "umin", "16b", a, b)
#define vmaxq_u8(a, b) __VCC_V3(uint8x16_t, "umax", "16b", a, b)
#define vceqq_u32(a, b) __VCC_V3(uint32x4_t, "cmeq", "4s", a, b)
#define vzip1q_u8(a, b) __VCC_V3(uint8x16_t, "zip1", "16b", a, b)
#define vcntq_u8(a) ({ uint8x16_t __r, __a = (a); asm("cnt %0.16b, %1.16b" : "=w"(__r) : "w"(__a)); __r; })
#define vcnt_u8(a) ({ uint8x8_t __r, __a = (a); asm("cnt %0.8b, %1.8b" : "=w"(__r) : "w"(__a)); __r; })
#define vpaddlq_u8(a) ({ uint8x16_t __a = (a); uint16x8_t __r; asm("uaddlp %0.8h, %1.16b" : "=w"(__r) : "w"(__a)); __r; })
#define vpaddlq_u16(a) ({ uint16x8_t __a = (a); uint32x4_t __r; asm("uaddlp %0.4s, %1.8h" : "=w"(__r) : "w"(__a)); __r; })
#define vpadalq_u32(a, b) ({ uint64x2_t __r = (a); uint32x4_t __b = (b); asm("uadalp %0.2d, %1.4s" : "+w"(__r) : "w"(__b)); __r; })
/* across lanes: the result is lane 0 of a vector register (the rest is zeroed) */
#define vmaxvq_u8(a) ({ uint8x16_t __r, __a = (a); asm("umaxv %b0, %1.16b" : "=w"(__r) : "w"(__a)); (uint8_t)__r[0]; })
#define vmaxvq_u32(a) ({ uint32x4_t __r, __a = (a); asm("umaxv %s0, %1.4s" : "=w"(__r) : "w"(__a)); (uint32_t)__r[0]; })
#define vaddvq_u16(a) ({ uint16x8_t __r, __a = (a); asm("addv %h0, %1.8h" : "=w"(__r) : "w"(__a)); (uint16_t)__r[0]; })
#define vaddv_u8(a) ({ uint8x8_t __r, __a = (a); asm("addv %b0, %1.8b" : "=w"(__r) : "w"(__a)); (uint8_t)__r[0]; })
#define vaddvq_u64(a) ({ uint64x2_t __r, __a = (a); asm("addp %d0, %1.2d" : "=w"(__r) : "w"(__a)); (uint64_t)__r[0]; })

#endif
