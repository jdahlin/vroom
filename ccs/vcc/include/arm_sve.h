/*
 * vcc's arm_sve.h: the SVE types and the intrinsics PostgreSQL's popcount
 * uses, each a macro around a statement expression with inline asm (as
 * arm_neon.h).  A sizeless value lives in a frame slot of the largest vector
 * length; an asm operand "w" loads it into z16+, "Upl" a predicate into p0+,
 * and z31/p15 are scratch.  A sizeless argument or result of a function is
 * not lowered, so nothing here is a function.  Every operand is evaluated
 * once and checked against the intrinsic's type by _Generic.
 *
 * The _x forms leave the inactive lanes undefined: the unpredicated
 * instruction where there is one.
 */
#ifndef __VCC_ARM_SVE_H
#define __VCC_ARM_SVE_H

#ifndef __aarch64__
#error "arm_sve.h needs an AArch64 target"
#endif

#include <stdint.h>

typedef __SVInt8_t svint8_t;
typedef __SVInt16_t svint16_t;
typedef __SVInt32_t svint32_t;
typedef __SVInt64_t svint64_t;
typedef __SVUint8_t svuint8_t;
typedef __SVUint16_t svuint16_t;
typedef __SVUint32_t svuint32_t;
typedef __SVUint64_t svuint64_t;
typedef __SVFloat16_t svfloat16_t;
typedef __SVFloat32_t svfloat32_t;
typedef __SVFloat64_t svfloat64_t;
typedef __SVBool_t svbool_t;

/* x, which must have the type T */
#define __vcc_sv(T, x) _Generic((x), T: (x))
/* a 64-bit general register operand from an integer of the parameter's type T */
#define __vcc_svx(T, x) ((uint64_t)(T)(x))

/* predicates and counts */
#define __VCC_SVPTRUE(sfx) ({ svbool_t __r; __asm__("ptrue %0." sfx : "=Upa"(__r)); __r; })
#define svptrue_b8() __VCC_SVPTRUE("b")
#define svptrue_b16() __VCC_SVPTRUE("h")
#define svptrue_b32() __VCC_SVPTRUE("s")
#define svptrue_b64() __VCC_SVPTRUE("d")
#define __VCC_SVCNT(op) ({ uint64_t __r; __asm__(op " %0" : "=r"(__r)); __r; })
#define svcntb() __VCC_SVCNT("cntb")
#define svcnth() __VCC_SVCNT("cnth")
#define svcntw() __VCC_SVCNT("cntw")
#define svcntd() __VCC_SVCNT("cntd")
#define svwhilelt_b8_s32(a, b) ({ svbool_t __r; \
	__asm__("whilelt %0.b, %w1, %w2" : "=Upa"(__r) : "r"((int32_t)(a)), "r"((int32_t)(b)) : "cc"); __r; })
#define svwhilelt_b8_s64(a, b) ({ svbool_t __r; \
	__asm__("whilelt %0.b, %1, %2" : "=Upa"(__r) : "r"((int64_t)(a)), "r"((int64_t)(b)) : "cc"); __r; })

/* every lane the scalar */
#define svdup_n_u8(x) ({ svuint8_t __r; __asm__("mov %0.b, %w1" : "=w"(__r) : "r"(__vcc_svx(uint8_t, x))); __r; })
#define svdup_n_u64(x) ({ svuint64_t __r; __asm__("mov %0.d, %1" : "=w"(__r) : "r"(__vcc_svx(uint64_t, x))); __r; })
#define svdup_u8(x) svdup_n_u8(x)
#define svdup_u64(x) svdup_n_u64(x)

/* loads: the inactive lanes are zero */
#define svld1_u8(pg, p) ({ svuint8_t __r; __asm__("ld1b {%0.b}, %1/z, [%2]" \
	: "=w"(__r) : "Upl"(__vcc_sv(svbool_t, pg)), "r"((const uint8_t *)(p))); __r; })
#define svld1_u64(pg, p) ({ svuint64_t __r; __asm__("ld1d {%0.d}, %1/z, [%2]" \
	: "=w"(__r) : "Upl"(__vcc_sv(svbool_t, pg)), "r"((const uint64_t *)(p))); __r; })

/* arithmetic */
#define __VCC_SVBIN_X(T, op, sfx, pg, a, b) ({ T __r; (void)__vcc_sv(svbool_t, pg); \
	__asm__(op " %0." sfx ", %1." sfx ", %2." sfx : "=w"(__r) : "w"(__vcc_sv(T, a)), "w"(__vcc_sv(T, b))); __r; })
#define svadd_u8_x(pg, a, b) __VCC_SVBIN_X(svuint8_t, "add", "b", pg, a, b)
#define svadd_u64_x(pg, a, b) __VCC_SVBIN_X(svuint64_t, "add", "d", pg, a, b)
#define svand_u8_x(pg, a, b) __VCC_SVBIN_X(svuint8_t, "and", "d", pg, a, b)
#define svand_u64_x(pg, a, b) __VCC_SVBIN_X(svuint64_t, "and", "d", pg, a, b)
#define svand_n_u8_x(pg, a, x) ({ svuint8_t __r; (void)__vcc_sv(svbool_t, pg); \
	__asm__("mov z31.b, %w2\n\tand %0.d, %1.d, z31.d" \
		: "=w"(__r) : "w"(__vcc_sv(svuint8_t, a)), "r"(__vcc_svx(uint8_t, x)) : "v31"); __r; })
#define svand_n_u64_x(pg, a, x) ({ svuint64_t __r; (void)__vcc_sv(svbool_t, pg); \
	__asm__("mov z31.d, %2\n\tand %0.d, %1.d, z31.d" \
		: "=w"(__r) : "w"(__vcc_sv(svuint64_t, a)), "r"(__vcc_svx(uint64_t, x)) : "v31"); __r; })

/* bits set per lane; the output is not an input, so no movprfx */
#define __VCC_SVCNT_X(T, sfx, pg, a) ({ T __r; __asm__("cnt %0." sfx ", %1/m, %2." sfx \
	: "=w"(__r) : "Upl"(__vcc_sv(svbool_t, pg)), "w"(__vcc_sv(T, a))); __r; })
#define svcnt_u8_x(pg, a) __VCC_SVCNT_X(svuint8_t, "b", pg, a)
#define svcnt_u64_x(pg, a) __VCC_SVCNT_X(svuint64_t, "d", pg, a)

/* the sum of the active lanes */
#define __VCC_SVADDV(T, sfx, pg, a) ({ uint64_t __r; __asm__("uaddv d31, %1, %2." sfx "\n\tfmov %0, d31" \
	: "=r"(__r) : "Upl"(__vcc_sv(svbool_t, pg)), "w"(__vcc_sv(T, a)) : "v31"); __r; })
#define svaddv_u8(pg, a) __VCC_SVADDV(svuint8_t, "b", pg, a)
#define svaddv_u64(pg, a) __VCC_SVADDV(svuint64_t, "d", pg, a)

#endif
