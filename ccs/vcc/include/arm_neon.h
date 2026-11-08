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

/* loads and stores */
#define __vcc_ld(T, p) ({ T __r; __builtin_memcpy(&__r, (p), sizeof __r); __r; })
#define __vcc_st(T, p, v) ({ T __v = (v); __builtin_memcpy((p), &__v, sizeof __v); (void)0; })
#define vld1q_u8(p) __vcc_ld(uint8x16_t, (const unsigned char *)(p))
#define vld1q_u64(p) __vcc_ld(uint64x2_t, (const unsigned long long *)(p))
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
#define vreinterpretq_u8_u64(x) ((uint8x16_t)(x))
#define vreinterpretq_p64_u64(x) ((poly64x2_t)(x))
#define vreinterpretq_u64_p64(x) ((uint64x2_t)(x))
#define vreinterpretq_u64_p128(x) ({ poly128_t __x = (x); uint64x2_t __r; __builtin_memcpy(&__r, &__x, 16); __r; })
#define vreinterpretq_p128_u64(x) ({ uint64x2_t __x = (x); poly128_t __r; __builtin_memcpy(&__r, &__x, 16); __r; })

#endif
