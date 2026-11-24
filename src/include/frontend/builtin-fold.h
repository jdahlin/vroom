#ifndef VCC_FRONTEND_BUILTIN_FOLD_H
#define VCC_FRONTEND_BUILTIN_FOLD_H
/*
 * builtin-fold.h: does a builtin fold in a constant expression, and how.
 *
 * One fact, kept in builtin-fold.inc: each spelling that a front end
 * evaluates in a constant expression, the named operation (vfold_op) it is
 * an instance of, and the languages that evaluate it.  Three things read it:
 *
 *   - __has_constexpr_builtin(NAME) is vfold_claim's answer where vcc
 *     implements the builtin (frontend/pp/builtin.cpp, the C++ front end's
 *     feature checks), so a claim is the row, never a second table;
 *   - the C front end folds a library call through the row's operation
 *     (frontend/c/constant-eval.cpp) and its builtin nodes through the same
 *     operations;
 *   - the C++ front end evaluates a registry row only where the row has the
 *     C++ bit (ext/gnu/builtin-registry.cpp, constant-evaluation.cpp).
 *
 * The operation is the key, not the spelling: __builtin_clz, clzl and clzll
 * are one operation at three widths, and so are fabs, fabsf and fabsl; the
 * width comes from the converted argument.  An operation's arithmetic that
 * does not depend on a front end's values is here too (vfold_integer,
 * vfold_rotate), so the two front ends cannot disagree about it.
 *
 * Portable C and C++, no library beyond <string.h>.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum vfold_op {
    vfold_none,
    /* the integer operations (vfold_integer, vfold_rotate) */
    vfold_clz,
    vfold_ctz,
    vfold_ffs,
    vfold_popcount,
    vfold_parity,
    vfold_clrsb,
    vfold_bswap,
    vfold_bitreverse,
    vfold_rotate_left,
    vfold_rotate_right,
    vfold_absolute,
    vfold_expect,
    /* the type-generic counts: clzg, ctzg, popcountg and C23's stdc_* */
    vfold_clz_generic,
    vfold_ctz_generic,
    vfold_popcount_generic,
    vfold_stdc_rotate_left,
    vfold_stdc_rotate_right,
    vfold_stdc_count_ones,
    vfold_stdc_trailing_zeros,
    /* the rest of C23's stdc_* and bitreverseg (C++ front end) */
    vfold_stdc_bits,
    /* the byte swap of any integer width */
    vfold_bswap_generic,
    /* clang's __builtin_align_up, _down and is_aligned (C++ front end) */
    vfold_align,
    /* the queries about the argument or its type */
    vfold_constant_p,
    vfold_lock_free,
    vfold_classify_type,
    vfold_object_size,
    vfold_identity,
    /* the floating operations */
    vfold_fabs,
    vfold_copysign,
    vfold_isnan,
    vfold_isinf,
    vfold_isinf_sign,
    vfold_isfinite,
    vfold_isnormal,
    vfold_signbit,
    vfold_fpclassify,
    vfold_isfpclass,
    vfold_iszero,
    vfold_issubnormal,
    vfold_issignaling,
    vfold_isgreater,
    vfold_isgreaterequal,
    vfold_isless,
    vfold_islessequal,
    vfold_islessgreater,
    vfold_isunordered,
    vfold_huge_val,
    vfold_nan,
    vfold_fp16_arithmetic,
    /* the string and memory functions on constant arrays */
    vfold_strlen,
    vfold_strcmp,
    vfold_strncmp,
    vfold_memcmp,
    vfold_memchr,
    vfold_wcslen,
    vfold_wmemcmp,
    vfold_wmemchr,
    /* the overflow checks and the add-with-carry */
    vfold_overflow,
    vfold_add_carry,
};

/* the languages that evaluate a row (VFOLD_NEITHER: a builtin that is
   implemented and folds nowhere, which is said so that clang's answer, which
   is for clang's builtin, is not taken for it) */
#define VFOLD_NEITHER 0
#define VFOLD_C 1
#define VFOLD_CXX 2
#define VFOLD_BOTH 3

typedef struct vfold_row {
    const char *name;
    uint8_t op;
    uint8_t languages;
} vfold_row;

/*
 * The row of a spelling, or NULL when no front end evaluates it.  The rows
 * are in no order; a lookup is a scan, made when a builtin is declared or a
 * feature macro is asked, not per evaluation.
 */
static inline const vfold_row *vfold_find(const char *name, size_t length)
{
    static const vfold_row rows[] = {
#define INTEGER_BUILTIN(spelling, id, flags, prototype, operation)             \
    {spelling, vfold_##operation, VFOLD_BOTH},
#include "frontend/integer-builtins.inc"
#undef INTEGER_BUILTIN
#define BUILTIN_FOLD(spelling, operation, languages)                           \
    {spelling, vfold_##operation, languages},
#include "frontend/builtin-fold.inc"
#undef BUILTIN_FOLD
    };

    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++)
        if (!strncmp(rows[i].name, name, length) && !rows[i].name[length])
            return &rows[i];
    return NULL;
}

/*
 * What __has_constexpr_builtin answers for a spelling vcc implements: 1 when
 * the language evaluates it, 0 when it does not, -1 when the spelling has
 * no row (the caller says 0 for a builtin vcc implements and has no row
 * for; clang's table answers for one vcc does not implement at all).
 */
static inline int vfold_claim(const char *name, size_t length, int cxx)
{
    const vfold_row *row = vfold_find(name, length);
    unsigned wanted = VFOLD_C;

    if (!row)
        return -1;
    if (cxx)
        wanted = VFOLD_CXX;
    return (row->languages & wanted) != 0;
}

/* whether the row is evaluated by the language (the gate of an evaluator) */
static inline int vfold_in_language(const vfold_row *row, int cxx)
{
    unsigned wanted = VFOLD_C;

    if (!row)
        return 0;
    if (cxx)
        wanted = VFOLD_CXX;
    return (row->languages & wanted) != 0;
}

/* ---- the integer operations, on the value's low `width` bits ----------- */

static inline unsigned __int128 vfold_mask(unsigned __int128 value,
                                           unsigned width)
{
    if (width >= 128)
        return value;
    return value & ((((unsigned __int128)1) << width) - 1);
}

static inline unsigned vfold_count_ones(unsigned __int128 value)
{
    return (unsigned)(__builtin_popcountll((uint64_t)value) +
                      __builtin_popcountll((uint64_t)(value >> 64)));
}

/* the leading zeros of a width-bit value (width when it is 0) */
static inline unsigned vfold_leading_zeros(unsigned __int128 value,
                                           unsigned width)
{
    unsigned n = 0;

    for (unsigned i = width; i-- > 0;) {
        if ((value >> i) & 1)
            break;
        n++;
    }
    return n;
}

/* the trailing zeros of a nonzero value */
static inline unsigned vfold_trailing_zeros(unsigned __int128 value)
{
    unsigned n = 0;

    while (!((value >> n) & 1))
        n++;
    return n;
}

/*
 * The integer operation op of the low width bits of value.  False when the
 * result is undefined and so not a constant: clz and ctz of 0 (clang's
 * ExprConstant says the same).  The result is not yet converted to the
 * call's type; for absolute it is the magnitude of the width-bit value read
 * as signed.
 */
static inline int vfold_integer(int op, unsigned __int128 value, unsigned width,
                                unsigned __int128 *result)
{
    unsigned __int128 bits = vfold_mask(value, width), r = 0;

    switch (op) {
    case vfold_clz:
        if (!bits)
            return 0;
        r = vfold_leading_zeros(bits, width);
        break;
    case vfold_ctz:
        if (!bits)
            return 0;
        r = vfold_trailing_zeros(bits);
        break;
    case vfold_ffs:
        if (bits)
            r = vfold_trailing_zeros(bits) + 1;
        break;
    case vfold_popcount:
        r = vfold_count_ones(bits);
        break;
    case vfold_parity:
        r = vfold_count_ones(bits) & 1;
        break;
    case vfold_clrsb: {
        /* the redundant sign bits: the leading bits equal to the sign bit,
           less that bit */
        unsigned __int128 s = bits;

        if ((bits >> (width - 1)) & 1)
            s = vfold_mask(~bits, width);
        r = vfold_leading_zeros(s, width) - 1;
        break;
    }
    case vfold_bswap:
        for (unsigned i = 0; i < width / 8; i++)
            r = (r << 8) | ((bits >> (8 * i)) & 0xff);
        break;
    case vfold_bitreverse:
        for (unsigned i = 0; i < width; i++)
            r = (r << 1) | ((bits >> i) & 1);
        break;
    case vfold_absolute: {
        __int128 s = (__int128)bits;

        if ((bits >> (width - 1)) & 1 && width < 128)
            s = (__int128)(bits | ~((((unsigned __int128)1) << width) - 1));
        if (s < 0)
            s = -s;
        r = (unsigned __int128)s;
        break;
    }
    default:
        return 0;
    }
    *result = r;
    return 1;
}

/* the width-bit value rotated by count (taken modulo the width) */
static inline unsigned __int128 vfold_rotate(unsigned __int128 value,
                                             unsigned width,
                                             unsigned __int128 count, int right)
{
    unsigned __int128 bits = vfold_mask(value, width);
    unsigned amount = (unsigned)(count % width);

    if (right)
        amount = (width - amount) % width;
    if (!amount)
        return bits;
    return vfold_mask((bits << amount) | (bits >> (width - amount)), width);
}

/* ---- C23's stdc_* and bitreverseg (vfold_stdc_bits) -------------------- */

/* the operations of the vfold_stdc_bits rows, C23 7.18.2-7.18.14 */
enum vfold_stdc_kind {
    vfold_stdc_leading_zeros,
    vfold_stdc_leading_ones,
    vfold_stdc_trailing_ones,
    vfold_stdc_first_leading_zero,
    vfold_stdc_first_leading_one,
    vfold_stdc_first_trailing_zero,
    vfold_stdc_first_trailing_one,
    vfold_stdc_count_zeros,
    vfold_stdc_bit_width,
    vfold_stdc_has_single_bit,
    vfold_stdc_bit_ceil,
    vfold_stdc_bit_floor,
    vfold_stdc_rotate_left_by,
    vfold_stdc_rotate_right_by,
    vfold_stdc_bitreverse,
};

/* the zero bits from the least significant end, width for a value with none */
static inline unsigned vfold_trailing_zeros_of(unsigned __int128 value,
                                               unsigned width)
{
    if (!value)
        return width;
    return vfold_trailing_zeros(value);
}

/*
 * The stdc operation kind of the low width bits of value (width 1 for a
 * bool).  A rotation's count is count, read as negative when
 * count_negative (a signed count rotates the other way when negative, by
 * its magnitude modulo the width); the other operations ignore it.  False
 * when the result is not a constant.  The result is not yet converted to
 * the call's type.
 */
static inline int vfold_stdc(int kind, unsigned __int128 value, unsigned width,
                             unsigned __int128 count, int count_negative,
                             unsigned __int128 *result)
{
    unsigned __int128 bits = vfold_mask(value, width);
    unsigned __int128 inverse = vfold_mask(~bits, width);
    unsigned __int128 r = 0;

    switch (kind) {
    case vfold_stdc_leading_zeros:
        r = vfold_leading_zeros(bits, width);
        break;
    case vfold_stdc_leading_ones:
        r = vfold_leading_zeros(inverse, width);
        break;
    case vfold_stdc_trailing_ones:
        r = vfold_trailing_zeros_of(inverse, width);
        break;
    case vfold_stdc_first_leading_zero:
        if (inverse)
            r = vfold_leading_zeros(inverse, width) + 1;
        break;
    case vfold_stdc_first_leading_one:
        if (bits)
            r = vfold_leading_zeros(bits, width) + 1;
        break;
    case vfold_stdc_first_trailing_zero:
        if (inverse)
            r = vfold_trailing_zeros(inverse) + 1;
        break;
    case vfold_stdc_first_trailing_one:
        if (bits)
            r = vfold_trailing_zeros(bits) + 1;
        break;
    case vfold_stdc_count_zeros:
        r = width - vfold_count_ones(bits);
        break;
    case vfold_stdc_bit_width:
        r = width - vfold_leading_zeros(bits, width);
        break;
    case vfold_stdc_has_single_bit:
        r = vfold_count_ones(bits) == 1;
        break;
    case vfold_stdc_bit_ceil:
        /* 1 for 0 and 1; the power of two above x - 1, which wraps to 0
           when the type has none (clang) */
        r = 1;
        if (bits > 1) {
            unsigned shift = width - vfold_leading_zeros(bits - 1, width);

            r = 0;
            if (shift < width)
                r = ((unsigned __int128)1) << shift;
        }
        break;
    case vfold_stdc_bit_floor:
        if (bits)
            r = ((unsigned __int128)1)
                << (width - 1 - vfold_leading_zeros(bits, width));
        break;
    case vfold_stdc_rotate_left_by:
    case vfold_stdc_rotate_right_by: {
        int right = kind == vfold_stdc_rotate_right_by;

        r = bits;
        if (width <= 1)
            break;
        if (count_negative) {
            count = 0 - count;
            right = !right;
        }
        r = vfold_rotate(bits, width, count % width, right);
        break;
    }
    case vfold_stdc_bitreverse:
        r = bits;
        if (width > 1 && !vfold_integer(vfold_bitreverse, bits, width, &r))
            return 0;
        break;
    default:
        return 0;
    }
    *result = r;
    return 1;
}

/* ---- clang's __builtin_align_up, _down and is_aligned (vfold_align) ---- */

enum vfold_align_kind {
    vfold_align_up,
    vfold_align_down,
    vfold_align_is_aligned,
};

/*
 * The align operation kind of an integer: modulo its width-bit type,
 * (x + a - 1) & ~(a - 1), x & ~(a - 1), x & (a - 1) == 0.  False unless
 * the alignment is a power of two of at most 2^(width-1) (clang: other
 * alignments are errors) and the width at most 64.
 */
static inline int vfold_align_integer(int kind, unsigned __int128 value,
                                      unsigned __int128 alignment,
                                      unsigned width, unsigned __int128 *result)
{
    unsigned __int128 bits, mask;

    if (!alignment || (alignment & (alignment - 1)) || width > 64 ||
        alignment > (((unsigned __int128)1) << (width - 1)))
        return 0;
    bits = vfold_mask(value, width);
    mask = vfold_mask(~(alignment - 1), width);
    switch (kind) {
    case vfold_align_up:
        *result = vfold_mask(bits + alignment - 1, width) & mask;
        return 1;
    case vfold_align_down:
        *result = bits & mask;
        return 1;
    default:
        *result = (bits & (alignment - 1)) == 0;
        return 1;
    }
}

#endif
