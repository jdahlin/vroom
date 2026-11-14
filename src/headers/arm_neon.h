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

/* Signed 128-bit tuples used by the ACLE zip/unzip intrinsics. */
typedef struct int8x16x2_t { int8x16_t val[2]; } int8x16x2_t;
typedef struct int16x8x2_t { int16x8_t val[2]; } int16x8x2_t;
typedef struct int32x4x2_t { int32x4_t val[2]; } int32x4x2_t;

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

/* Signed load/store forms preserve the unaligned ACLE memory semantics. */
#define vld1q_s8(p) __vcc_ld(int8x16_t, (const int8_t *)(p))
#define vld1q_s16(p) __vcc_ld(int16x8_t, (const int16_t *)(p))
#define vld1q_s32(p) __vcc_ld(int32x4_t, (const int32_t *)(p))
#define vst1q_s8(p, v) __vcc_st(int8x16_t, (int8_t *)(p), v)
#define vst1q_s16(p, v) __vcc_st(int16x8_t, (int16_t *)(p), v)
#define vst1q_s32(p, v) __vcc_st(int32x4_t, (int32_t *)(p), v)
#define vdupq_n_s8(x) ((int8x16_t)vdupq_n_u8((uint8_t)(x)))
#define vdupq_n_s16(x) ({ int16_t __s = (x); int16x8_t __r = { __s, __s, __s, __s, __s, __s, __s, __s }; __r; })
#define vdupq_n_s32(x) ((int32x4_t)vdupq_n_u32((uint32_t)(x)))
#define vmovq_n_s8(x) vdupq_n_s8(x)
#define vmovq_n_s16(x) vdupq_n_s16(x)
#define vmovq_n_s32(x) vdupq_n_s32(x)
#define vaddq_s8(a, b) ((int8x16_t)((uint8x16_t)(a) + (uint8x16_t)(b)))
#define vaddq_s16(a, b) ((int16x8_t)((uint16x8_t)(a) + (uint16x8_t)(b)))
#define vaddq_s32(a, b) ((int32x4_t)((uint32x4_t)(a) + (uint32x4_t)(b)))

/* ACLE's pair forms return two vectors, and evaluate each operand once.
 * The first output must not overwrite either input before instruction two. */
#ifdef __aarch64__
#define __VCC_PAIR(T, V, N, op, arr, a, b) ({ V __pa = (a), __pb = (b); T __pr; \
 __asm__(op "1 %0." arr ", %2." arr ", %3." arr "\n\t" \
     op "2 %1." arr ", %2." arr ", %3." arr \
     : "=&w"(__pr.val[0]), "=w"(__pr.val[1]) : "w"(__pa), "w"(__pb)); __pr; })
#define __VCC_ZIP(T, V, N, arr, a, b) __VCC_PAIR(T, V, N, "zip", arr, a, b)
#define __VCC_UZP(T, V, N, arr, a, b) __VCC_PAIR(T, V, N, "uzp", arr, a, b)
#else
#define __VCC_ZIP(T, V, N, arr, a, b) ({ V __pa = (a), __pb = (b); T __pr; \
 for (int __k = 0; __k < (N); ++__k) { \
   __pr.val[__k / ((N)/2)][2 * (__k % ((N)/2))] = __pa[__k]; \
   __pr.val[__k / ((N)/2)][2 * (__k % ((N)/2)) + 1] = __pb[__k]; } __pr; })
#define __VCC_UZP(T, V, N, arr, a, b) ({ V __pa = (a), __pb = (b); T __pr; \
 for (int __k = 0; __k < (N); ++__k) { \
   __pr.val[__k & 1][__k / 2] = __pa[__k]; \
   __pr.val[__k & 1][__k / 2 + (N)/2] = __pb[__k]; } __pr; })
#endif
#define vzipq_s8(a, b) __VCC_ZIP(int8x16x2_t, int8x16_t, 16, "16b", a, b)
#define vzipq_s16(a, b) __VCC_ZIP(int16x8x2_t, int16x8_t, 8, "8h", a, b)
#define vzipq_s32(a, b) __VCC_ZIP(int32x4x2_t, int32x4_t, 4, "4s", a, b)
#define vuzpq_s8(a, b) __VCC_UZP(int8x16x2_t, int8x16_t, 16, "16b", a, b)
#define vuzpq_s16(a, b) __VCC_UZP(int16x8x2_t, int16x8_t, 8, "8h", a, b)
#define vuzpq_s32(a, b) __VCC_UZP(int32x4x2_t, int32x4_t, 4, "4s", a, b)

/* bitwise */
#define veorq_u8(a, b) ((uint8x16_t)(a) ^ (uint8x16_t)(b))
#define veorq_u64(a, b) ((uint64x2_t)(a) ^ (uint64x2_t)(b))
#define vandq_u8(a, b) ((uint8x16_t)(a) & (uint8x16_t)(b))
#define vandq_u64(a, b) ((uint64x2_t)(a) & (uint64x2_t)(b))
#define vorrq_u8(a, b) ((uint8x16_t)(a) | (uint8x16_t)(b))
#define vorrq_u64(a, b) ((uint64x2_t)(a) | (uint64x2_t)(b))

/* lanes and moves */
#define vdupq_n_u8(x) ({ uint8x16_t __r; __asm__("dup %0.16b, %w1" : "=w"(__r) : "r"((unsigned int)(unsigned char)(x))); __r; })
#define vdupq_n_u64(x) ({ uint64x2_t __r; __asm__("dup %0.2d, %x1" : "=w"(__r) : "r"((unsigned long long)(x))); __r; })
#define vgetq_lane_u64(v, i) ({ uint64x2_t __v = (v); __v[(i) & 1]; })
#define vgetq_lane_u8(v, i) ({ uint8x16_t __v = (v); __v[(i) & 15]; })
#define vget_low_u8(v) ({ uint8x16_t __v = (v); uint8x8_t __r; __builtin_memcpy(&__r, &__v, 8); __r; })
#define vget_high_u8(v) ({ uint8x16_t __v = (v); uint8x8_t __r; __builtin_memcpy(&__r, (char *)&__v + 8, 8); __r; })
#define vcombine_u8(l, h) ({ uint8x8_t __l = (l), __h = (h); uint8x16_t __r; __builtin_memcpy(&__r, &__l, 8); \
	__builtin_memcpy((char *)&__r + 8, &__h, 8); __r; })
#define vextq_u8(a, b, n) ({ uint8x16_t __r, __a = (a), __b = (b); \
	__asm__("ext %0.16b, %1.16b, %2.16b, %3" : "=w"(__r) : "w"(__a), "w"(__b), "i"(n)); __r; })
#define vextq_u64(a, b, n) ({ uint64x2_t __r, __a = (a), __b = (b); \
	__asm__("ext %0.16b, %1.16b, %2.16b, %3" : "=w"(__r) : "w"(__a), "w"(__b), "i"(8 * (n))); __r; })

/* shifts */
#define vshrq_n_u8(a, n) ({ uint8x16_t __r, __a = (a); __asm__("ushr %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshrq_n_s8(a, n) ({ int8x16_t __r, __a = (a); __asm__("sshr %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshlq_n_u8(a, n) ({ uint8x16_t __r, __a = (a); __asm__("shl %0.16b, %1.16b, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshrq_n_u64(a, n) ({ uint64x2_t __r, __a = (a); __asm__("ushr %0.2d, %1.2d, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vshlq_n_u64(a, n) ({ uint64x2_t __r, __a = (a); __asm__("shl %0.2d, %1.2d, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })

/* table lookups and polynomial multiplies */
#define vqtbl1q_u8(t, i) ({ uint8x16_t __r, __t = (t), __i = (i); \
	__asm__("tbl %0.16b, {%1.16b}, %2.16b" : "=w"(__r) : "w"(__t), "w"(__i)); __r; })
#define vqtbl4q_u8(t, i) ({ uint8x16x4_t __t = (t); uint8x16_t __i = (i), __r; const unsigned char *__b = (const unsigned char *)&__t; \
	for (int __k = 0; __k < 16; __k++) __r[__k] = __i[__k] < 64 ? __b[__i[__k]] : 0; __r; })
#define vtbl2_u8(t, i) ({ uint8x8x2_t __t = (t); uint8x8_t __i = (i), __r; uint8x16_t __q = vcombine_u8(__t.val[0], __t.val[1]); \
	__asm__("tbl %0.8b, {%1.16b}, %2.8b" : "=w"(__r) : "w"(__q), "w"(__i)); __r; })
#define vmulq_p8(a, b) ({ poly8x16_t __r, __a = (a), __b = (b); \
	__asm__("pmul %0.16b, %1.16b, %2.16b" : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vmull_p64(a, b) ({ uint64x2_t __a = { (a), 0 }, __b = { (b), 0 }, __r; \
	__asm__(".arch_extension aes\n\tpmull %0.1q, %1.1d, %2.1d" : "=w"(__r) : "w"(__a), "w"(__b)); \
	poly128_t __p; __builtin_memcpy(&__p, &__r, 16); __p; })
#define vmull_high_p64(a, b) ({ poly64x2_t __a = (a), __b = (b); uint64x2_t __r; \
	__asm__(".arch_extension aes\n\tpmull2 %0.1q, %1.2d, %2.2d" : "=w"(__r) : "w"(__a), "w"(__b)); \
	poly128_t __p; __builtin_memcpy(&__p, &__r, 16); __p; })

/* reinterpretations */
#define vreinterpretq_u64_u8(x) ((uint64x2_t)(x))
#define vreinterpretq_u16_u8(x) ((uint16x8_t)(x))
#define vreinterpret_u64_u8(x) ((uint64x1_t)(x))
#define vget_lane_u64(v, i) ({ uint64x1_t __v = (v); (void)(i); __v[0]; })
#define vshrn_n_u16(a, n) ({ uint16x8_t __a = (a); uint8x8_t __r; \
	for (int __k = 0; __k < 8; __k++) __r[__k] = (unsigned char)(__a[__k] >> (n)); __r; })
#define vsriq_n_u8(a, b, n) ({ uint8x16_t __r = (a), __b = (b); __asm__("sri %0.16b, %1.16b, %2" : "+w"(__r) : "w"(__b), "i"(n)); __r; })
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
	__asm__(op " %0.16b, %1.16b, %2.16b" : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vceqq_u8(a, b) __VCC_CMP("cmeq", a, b)
#define vtstq_u8(a, b) __VCC_CMP("cmtst", a, b)
#define vcleq_u8(a, b) __VCC_CMP("cmhs", b, a)
#define vceqzq_u8(a) vceqq_u8(a, vdupq_n_u8(0))
#define vld4q_u8(p) ({ const unsigned char *__p = (const unsigned char *)(p); uint8x16x4_t __r; \
	for (int __k = 0; __k < 64; __k++) __r.val[__k & 3][__k >> 2] = __p[__k]; __r; })

/* used by PostgreSQL (port/simd.h, pg_popcount_aarch64.c) */
#define vld1q_u32(p) __vcc_ld(uint32x4_t, (const unsigned int *)(p))
#define vdupq_n_u32(x) ({ uint32x4_t __r; __asm__("dup %0.4s, %w1" : "=w"(__r) : "r"((unsigned int)(x))); __r; })
#define vorrq_u32(a, b) ((uint32x4_t)(a) | (uint32x4_t)(b))
#define vaddq_u64(a, b) ((uint64x2_t)(a) + (uint64x2_t)(b))
#define __VCC_V3(T, op, arr, a, b) ({ T __r, __a = (a), __b = (b); \
	__asm__(op " %0." arr ", %1." arr ", %2." arr : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vqsubq_u8(a, b) __VCC_V3(uint8x16_t, "uqsub", "16b", a, b)
#define vminq_u8(a, b) __VCC_V3(uint8x16_t, "umin", "16b", a, b)
#define vmaxq_u8(a, b) __VCC_V3(uint8x16_t, "umax", "16b", a, b)
#define vceqq_u32(a, b) __VCC_V3(uint32x4_t, "cmeq", "4s", a, b)
#define vzip1q_u8(a, b) __VCC_V3(uint8x16_t, "zip1", "16b", a, b)
#define vcntq_u8(a) ({ uint8x16_t __r, __a = (a); __asm__("cnt %0.16b, %1.16b" : "=w"(__r) : "w"(__a)); __r; })
#define vcnt_u8(a) ({ uint8x8_t __r, __a = (a); __asm__("cnt %0.8b, %1.8b" : "=w"(__r) : "w"(__a)); __r; })
#define vpaddlq_u8(a) ({ uint8x16_t __a = (a); uint16x8_t __r; __asm__("uaddlp %0.8h, %1.16b" : "=w"(__r) : "w"(__a)); __r; })
#define vpaddlq_u16(a) ({ uint16x8_t __a = (a); uint32x4_t __r; __asm__("uaddlp %0.4s, %1.8h" : "=w"(__r) : "w"(__a)); __r; })
#define vpadalq_u32(a, b) ({ uint64x2_t __r = (a); uint32x4_t __b = (b); __asm__("uadalp %0.2d, %1.4s" : "+w"(__r) : "w"(__b)); __r; })
/* across lanes: the result is lane 0 of a vector register (the rest is zeroed) */
#define vmaxvq_u8(a) ({ uint8x16_t __r, __a = (a); __asm__("umaxv %b0, %1.16b" : "=w"(__r) : "w"(__a)); (uint8_t)__r[0]; })
#define vmaxvq_u32(a) ({ uint32x4_t __r, __a = (a); __asm__("umaxv %s0, %1.4s" : "=w"(__r) : "w"(__a)); (uint32_t)__r[0]; })
#define vaddvq_u16(a) ({ uint16x8_t __r, __a = (a); __asm__("addv %h0, %1.8h" : "=w"(__r) : "w"(__a)); (uint16_t)__r[0]; })
#define vaddv_u8(a) ({ uint8x8_t __r, __a = (a); __asm__("addv %b0, %1.8b" : "=w"(__r) : "w"(__a)); (uint8_t)__r[0]; })
#define vaddvq_u64(a) ({ uint64x2_t __r, __a = (a); __asm__("addp %d0, %1.2d" : "=w"(__r) : "w"(__a)); (uint64_t)__r[0]; })
#define vqtbx4q_u8(d, t, i) ({ uint8x16_t __r = (d), __i = (i); uint8x16x4_t __t = (t); \
	const unsigned char *__b = (const unsigned char *)&__t; \
	for (int __k = 0; __k < 16; __k++) if (__i[__k] < 64) __r[__k] = __b[__i[__k]]; __r; })

/* used by vcc's own linker (ldhash.c: BLAKE3 four lanes at a time) */
#define vaddq_u32(a, b) ((uint32x4_t)(a) + (uint32x4_t)(b))
#define veorq_u32(a, b) ((uint32x4_t)(a) ^ (uint32x4_t)(b))
#define vst1q_u32(p, v) __vcc_st(uint32x4_t, (unsigned int *)(p), v)
#define vreinterpretq_u16_u32(x) ((uint16x8_t)(x))
#define vreinterpretq_u32_u16(x) ((uint32x4_t)(x))
#define vreinterpretq_u32_u64(x) ((uint32x4_t)(x))
#define vreinterpretq_u32_u8(x) ((uint32x4_t)(x))
#define vreinterpretq_u64_u32(x) ((uint64x2_t)(x))
#define vreinterpretq_u8_u32(x) ((uint8x16_t)(x))
#define vrev32q_u16(a) ({ uint16x8_t __r, __a = (a); __asm__("rev32 %0.8h, %1.8h" : "=w"(__r) : "w"(__a)); __r; })
#define vshlq_n_u32(a, n) ({ uint32x4_t __r, __a = (a); __asm__("shl %0.4s, %1.4s, %2" : "=w"(__r) : "w"(__a), "i"(n)); __r; })
#define vsriq_n_u32(a, b, n) ({ uint32x4_t __r = (a), __b = (b); __asm__("sri %0.4s, %1.4s, %2" : "+w"(__r) : "w"(__b), "i"(n)); __r; })
#define vtrn1q_u32(a, b) __VCC_V3(uint32x4_t, "trn1", "4s", a, b)
#define vtrn2q_u32(a, b) __VCC_V3(uint32x4_t, "trn2", "4s", a, b)
#define vtrn1q_u64(a, b) __VCC_V3(uint64x2_t, "trn1", "2d", a, b)
#define vtrn2q_u64(a, b) __VCC_V3(uint64x2_t, "trn2", "2d", a, b)

/* used by crypto/aegis128-neon-inner.c and its arm32 fallbacks */
#define vqtbx1q_u8(d, t, i) ({ uint8x16_t __r = (d), __t = (t), __i = (i); \
	__asm__("tbx %0.16b, {%1.16b}, %2.16b" : "+w"(__r) : "w"(__t), "w"(__i)); __r; })
#define vtbx2_u8(d, t, i) ({ uint8x8x2_t __t = (t); uint8x8_t __r = (d), __i = (i); uint8x16_t __q = vcombine_u8(__t.val[0], __t.val[1]); \
	__asm__("tbx %0.8b, {%1.16b}, %2.8b" : "+w"(__r) : "w"(__q), "w"(__i)); __r; })
#define vminvq_s8(a) ({ int8x16_t __r, __a = (a); __asm__("sminv %b0, %1.16b" : "=w"(__r) : "w"(__a)); (int8_t)__r[0]; })
#define vpmin_s8(a, b) ({ int8x8_t __r, __a = (a), __b = (b); \
	__asm__("sminp %0.8b, %1.8b, %2.8b" : "=w"(__r) : "w"(__a), "w"(__b)); __r; })
#define vmov_n_u64(x) ((uint64x1_t){ (unsigned long long)(x) })
#define vcombine_u64(l, h) ({ uint64x1_t __l = (l), __h = (h); uint64x2_t __r = { __l[0], __h[0] }; __r; })
#define vget_low_s8(v) ((int8x8_t)vget_low_u8((uint8x16_t)(v)))
#define vget_high_s8(v) ((int8x8_t)vget_high_u8((uint8x16_t)(v)))
#define vget_lane_s8(v, i) ({ int8x8_t __v = (v); __v[(i) & 7]; })

/*
 * Other targets (vcc built for WebAssembly by itself, W6): the intrinsics
 * written as inline asm above, lane by lane in C with the same results.
 */
#ifndef __aarch64__
#define __VCC_L1(T, n, a, e) ({ T __r, __a = (a); for (int __k = 0; __k < (n); __k++) __r[__k] = (e); __r; })
#define __VCC_L2(T, n, a, b, e) ({ T __r, __a = (a), __b = (b); for (int __k = 0; __k < (n); __k++) __r[__k] = (e); __r; })
#define __VCC_POP8(x) ({ unsigned __x = (x); __x = __x - ((__x >> 1) & 0x55); __x = (__x & 0x33) + ((__x >> 2) & 0x33); \
	(unsigned char)((__x + (__x >> 4)) & 0x0f); })
#undef vdupq_n_u8
#undef vdupq_n_u64
#undef vdupq_n_u32
#undef vextq_u8
#undef vextq_u64
#undef vshrq_n_u8
#undef vshrq_n_s8
#undef vshlq_n_u8
#undef vshrq_n_u64
#undef vshlq_n_u64
#undef vqtbl1q_u8
#undef vtbl2_u8
#undef vmulq_p8
#undef vmull_p64
#undef vmull_high_p64
#undef vsriq_n_u8
#undef __VCC_CMP
#undef vceqq_u8
#undef vtstq_u8
#undef vcleq_u8
#undef __VCC_V3
#undef vqsubq_u8
#undef vminq_u8
#undef vmaxq_u8
#undef vceqq_u32
#undef vzip1q_u8
#undef vcntq_u8
#undef vcnt_u8
#undef vpaddlq_u8
#undef vpaddlq_u16
#undef vpadalq_u32
#undef vmaxvq_u8
#undef vmaxvq_u32
#undef vaddvq_u16
#undef vaddv_u8
#undef vaddvq_u64
#undef vrev32q_u16
#undef vshlq_n_u32
#undef vsriq_n_u32
#undef vtrn1q_u32
#undef vtrn2q_u32
#undef vtrn1q_u64
#undef vtrn2q_u64
#undef vqtbx1q_u8
#undef vtbx2_u8
#undef vminvq_s8
#undef vpmin_s8
#define vdupq_n_u8(x) ({ unsigned char __x = (unsigned char)(x); uint8x16_t __r; for (int __k = 0; __k < 16; __k++) __r[__k] = __x; __r; })
#define vdupq_n_u32(x) ({ unsigned int __x = (unsigned int)(x); uint32x4_t __r = { __x, __x, __x, __x }; __r; })
#define vdupq_n_u64(x) ({ unsigned long long __x = (unsigned long long)(x); uint64x2_t __r = { __x, __x }; __r; })
#define vextq_u8(a, b, n) __VCC_L2(uint8x16_t, 16, a, b, __k + (n) < 16 ? __a[__k + (n)] : __b[__k + (n) - 16])
#define vextq_u64(a, b, n) __VCC_L2(uint64x2_t, 2, a, b, __k + (n) < 2 ? __a[__k + (n)] : __b[__k + (n) - 2])
#define vshrq_n_u8(a, n) __VCC_L1(uint8x16_t, 16, a, (unsigned char)(__a[__k] >> (n)))
#define vshrq_n_s8(a, n) __VCC_L1(int8x16_t, 16, a, (signed char)(__a[__k] >> (n)))
#define vshlq_n_u8(a, n) __VCC_L1(uint8x16_t, 16, a, (unsigned char)(__a[__k] << (n)))
#define vshrq_n_u64(a, n) __VCC_L1(uint64x2_t, 2, a, __a[__k] >> (n))
#define vshlq_n_u64(a, n) __VCC_L1(uint64x2_t, 2, a, __a[__k] << (n))
#define vshlq_n_u32(a, n) __VCC_L1(uint32x4_t, 4, a, __a[__k] << (n))
#define vqtbl1q_u8(t, i) __VCC_L2(uint8x16_t, 16, t, i, __b[__k] < 16 ? __a[__b[__k]] : 0)
#define vtbl2_u8(t, i) ({ uint8x8x2_t __t = (t); uint8x16_t __q = vcombine_u8(__t.val[0], __t.val[1]); \
	uint8x8_t __i = (i), __r; for (int __k = 0; __k < 8; __k++) __r[__k] = __i[__k] < 16 ? __q[__i[__k]] : 0; __r; })
#define vmulq_p8(a, b) ({ poly8x16_t __r, __a = (a), __b = (b); for (int __k = 0; __k < 16; __k++) { unsigned __p = 0; \
	for (int __j = 0; __j < 8; __j++) if (__b[__k] >> __j & 1) __p ^= (unsigned)__a[__k] << __j; __r[__k] = (unsigned char)__p; } __r; })
#define __VCC_PMULL(x, y) ({ unsigned long long __x = (x), __y = (y); poly128_t __p = 0; \
	for (int __j = 0; __j < 64; __j++) if (__y >> __j & 1) __p ^= (poly128_t)__x << __j; __p; })
#define vmull_p64(a, b) __VCC_PMULL(a, b)
#define vmull_high_p64(a, b) ({ poly64x2_t __ha = (a), __hb = (b); __VCC_PMULL(__ha[1], __hb[1]); })
#define vsriq_n_u8(a, b, n) __VCC_L2(uint8x16_t, 16, a, b, (unsigned char)((__a[__k] & ~(0xffu >> (n))) | (__b[__k] >> (n))))
#define vsriq_n_u32(a, b, n) __VCC_L2(uint32x4_t, 4, a, b, (n) >= 32 ? __a[__k] : (__a[__k] & ~(0xffffffffu >> (n))) | (__b[__k] >> (n)))
#define vceqq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __a[__k] == __b[__k] ? 0xff : 0)
#define vtstq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, (__a[__k] & __b[__k]) ? 0xff : 0)
#define vcleq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __a[__k] <= __b[__k] ? 0xff : 0)
#define vqsubq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __a[__k] > __b[__k] ? __a[__k] - __b[__k] : 0)
#define vminq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __a[__k] < __b[__k] ? __a[__k] : __b[__k])
#define vmaxq_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __a[__k] > __b[__k] ? __a[__k] : __b[__k])
#define vceqq_u32(a, b) __VCC_L2(uint32x4_t, 4, a, b, __a[__k] == __b[__k] ? 0xffffffffu : 0)
#define vzip1q_u8(a, b) __VCC_L2(uint8x16_t, 16, a, b, __k & 1 ? __b[__k >> 1] : __a[__k >> 1])
#define vtrn1q_u32(a, b) __VCC_L2(uint32x4_t, 4, a, b, __k & 1 ? __b[__k - 1] : __a[__k])
#define vtrn2q_u32(a, b) __VCC_L2(uint32x4_t, 4, a, b, __k & 1 ? __b[__k] : __a[__k + 1])
#define vtrn1q_u64(a, b) __VCC_L2(uint64x2_t, 2, a, b, __k ? __b[0] : __a[0])
#define vtrn2q_u64(a, b) __VCC_L2(uint64x2_t, 2, a, b, __k ? __b[1] : __a[1])
#define vrev32q_u16(a) __VCC_L1(uint16x8_t, 8, a, __a[__k ^ 1])
#define vcntq_u8(a) __VCC_L1(uint8x16_t, 16, a, __VCC_POP8(__a[__k]))
#define vcnt_u8(a) __VCC_L1(uint8x8_t, 8, a, __VCC_POP8(__a[__k]))
#define vpaddlq_u8(a) ({ uint8x16_t __a = (a); uint16x8_t __r; for (int __k = 0; __k < 8; __k++) \
	__r[__k] = (unsigned short)(__a[2 * __k] + __a[2 * __k + 1]); __r; })
#define vpaddlq_u16(a) ({ uint16x8_t __a = (a); uint32x4_t __r; for (int __k = 0; __k < 4; __k++) \
	__r[__k] = (unsigned)__a[2 * __k] + __a[2 * __k + 1]; __r; })
#define vpadalq_u32(a, b) ({ uint64x2_t __r = (a); uint32x4_t __b = (b); for (int __k = 0; __k < 2; __k++) \
	__r[__k] += (unsigned long long)__b[2 * __k] + __b[2 * __k + 1]; __r; })
#define vmaxvq_u8(a) ({ uint8x16_t __a = (a); uint8_t __m = 0; for (int __k = 0; __k < 16; __k++) if (__a[__k] > __m) __m = __a[__k]; __m; })
#define vmaxvq_u32(a) ({ uint32x4_t __a = (a); uint32_t __m = 0; for (int __k = 0; __k < 4; __k++) if (__a[__k] > __m) __m = __a[__k]; __m; })
#define vaddvq_u16(a) ({ uint16x8_t __a = (a); uint16_t __s = 0; for (int __k = 0; __k < 8; __k++) __s += __a[__k]; __s; })
#define vaddv_u8(a) ({ uint8x8_t __a = (a); uint8_t __s = 0; for (int __k = 0; __k < 8; __k++) __s += __a[__k]; __s; })
#define vaddvq_u64(a) ({ uint64x2_t __a = (a); (uint64_t)(__a[0] + __a[1]); })
#define vqtbx1q_u8(d, t, i) ({ uint8x16_t __r = (d), __t = (t), __i = (i); \
	for (int __k = 0; __k < 16; __k++) if (__i[__k] < 16) __r[__k] = __t[__i[__k]]; __r; })
#define vtbx2_u8(d, t, i) ({ uint8x8x2_t __t = (t); uint8x8_t __r = (d), __i = (i); uint8x16_t __q = vcombine_u8(__t.val[0], __t.val[1]); \
	for (int __k = 0; __k < 8; __k++) if (__i[__k] < 16) __r[__k] = __q[__i[__k]]; __r; })
#define vminvq_s8(a) ({ int8x16_t __a = (a); int8_t __m = __a[0]; for (int __k = 1; __k < 16; __k++) if (__a[__k] < __m) __m = __a[__k]; __m; })
#define vpmin_s8(a, b) ({ int8x8_t __r, __a = (a), __b = (b); for (int __k = 0; __k < 4; __k++) { \
	__r[__k] = __a[2 * __k] < __a[2 * __k + 1] ? __a[2 * __k] : __a[2 * __k + 1]; \
	__r[__k + 4] = __b[2 * __k] < __b[2 * __k + 1] ? __b[2 * __k] : __b[2 * __k + 1]; } __r; })
#endif

#endif
