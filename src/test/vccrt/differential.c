/*
 * The differential test of vcc's run-time library (bench/vccrt-test.py):
 * calls every helper by its name on generated inputs and prints, per
 * helper, the number of calls and a digest of every result's bytes.  The
 * same program is linked once with libvccrt.a and once with compiler-rt's
 * builtins; the two outputs must be equal.  `differential N` uses N random
 * inputs per helper (after the special values); `differential N HELPER`
 * prints that helper's every call instead: the inputs and the result in hex.
 *
 * Inputs: special values first (zeros, ones, extremes, powers of two and
 * their neighbours, for floating formats the subnormal and normal bounds,
 * infinities, quiet and signalling NaNs with payloads, values half way
 * between two results of a narrowing), then random ones from several
 * shapes (all bits random, random length, few bits set, exponents near the
 * ends of the range, pairs with close exponents for cancellation).
 * Inputs whose result C leaves undefined are not used: a zero divisor, a
 * conversion to a signed integer whose magnitude needs the sign bit.
 *
 * The driver itself computes nothing on __int128 or the floating types that
 * a compiler would turn into one of the helpers under test (it works on
 * bits), so both sides see the same inputs whatever the library does.
 * Compiled with -DNO_TRUNCXFBF2 for compiler-rt, which has no __truncxfbf2.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned __int128 u128;
typedef __int128 i128;
typedef unsigned long long u64;

/* ---------------- the helpers under test ---------------- */

u128 __udivti3(u128, u128);
u128 __umodti3(u128, u128);
i128 __divti3(i128, i128);
i128 __modti3(i128, i128);
i128 __ashlti3(i128, int);
i128 __ashrti3(i128, int);
i128 __lshrti3(i128, int);
float __floattisf(i128);
double __floattidf(i128);
long double __floattixf(i128);
float __floatuntisf(u128);
double __floatuntidf(u128);
long double __floatuntixf(u128);
i128 __fixsfti(float);
i128 __fixdfti(double);
i128 __fixxfti(long double);
u128 __fixunssfti(float);
u128 __fixunsdfti(double);
u128 __fixunsxfti(long double);
__float128 __addtf3(__float128, __float128);
__float128 __subtf3(__float128, __float128);
__float128 __multf3(__float128, __float128);
__float128 __divtf3(__float128, __float128);
int __eqtf2(__float128, __float128);
int __netf2(__float128, __float128);
int __lttf2(__float128, __float128);
int __letf2(__float128, __float128);
int __gttf2(__float128, __float128);
int __getf2(__float128, __float128);
int __unordtf2(__float128, __float128);
__float128 __extendsftf2(float);
__float128 __extenddftf2(double);
__float128 __extendxftf2(long double);
float __trunctfsf2(__float128);
double __trunctfdf2(__float128);
long double __trunctfxf2(__float128);
__float128 __floatsitf(int);
__float128 __floatditf(long long);
__float128 __floattitf(i128);
__float128 __floatunsitf(unsigned);
__float128 __floatunditf(unsigned long long);
__float128 __floatuntitf(u128);
int __fixtfsi(__float128);
long long __fixtfdi(__float128);
i128 __fixtfti(__float128);
unsigned __fixunstfsi(__float128);
unsigned long long __fixunstfdi(__float128);
u128 __fixunstfti(__float128);
float __extendhfsf2(_Float16);
float __extendbfsf2(__bf16);
_Float16 __truncsfhf2(float);
_Float16 __truncdfhf2(double);
_Float16 __truncxfhf2(long double);
_Float16 __trunctfhf2(__float128);
__bf16 __truncsfbf2(float);
__bf16 __truncdfbf2(double);
__bf16 __truncxfbf2(long double);
__bf16 __trunctfbf2(__float128);
_Complex float __mulsc3(float, float, float, float);
_Complex double __muldc3(double, double, double, double);
_Complex long double __mulxc3(long double, long double, long double, long double);
_Complex __float128 __multc3(__float128, __float128, __float128, __float128);
_Complex float __divsc3(float, float, float, float);
_Complex double __divdc3(double, double, double, double);
_Complex long double __divxc3(long double, long double, long double, long double);
_Complex __float128 __divtc3(__float128, __float128, __float128, __float128);
float __powisf2(float, int);
double __powidf2(double, int);
long double __powixf2(long double, int);
__float128 __powitf2(__float128, int);

/* ---------------- inputs ---------------- */

static u64 state = 0x9e3779b97f4a7c15ull;

/* splitmix64 */
static u64 next(void)
{
	u64 z = (state += 0x9e3779b97f4a7c15ull);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
	return z ^ (z >> 31);
}

static unsigned below(unsigned n)
{
	return (unsigned)(next() % n);
}

static u128 ones(int bits)
{
	return bits >= 128 ? ~(u128)0 : bits <= 0 ? 0 : ((u128)1 << bits) - 1;
}

static u128 random128(void)
{
	return (u128)next() << 64 | next();
}

/* an integer of a random shape */
static u128 integer_input(long i)
{
	static const int shapes[] = { 0, 1, 2, 3, 4, 5 };
	u128 x;
	if (i < 128 * 3) {	/* 2^k - 1, 2^k, 2^k + 1 */
		x = (u128)1 << (i / 3);
		return x + (u128)(i % 3) - 1;
	}
	if (i < 128 * 3 + 8) {
		static const u128 fixed[] = { 0, 1, 2, 3, 10, ~(u128)0 >> 1, ((u128)1 << 127) + 1, ~(u128)0 - 1 };
		return fixed[i - 128 * 3];
	}
	switch (shapes[below(6)]) {
	case 0:
		return random128();
	case 1:
		return random128() & ones(1 + below(128));
	case 2:
		return ~(random128() & ones(1 + below(128)));	/* negative, small magnitude */
	case 3:
		x = 0;
		for (int k = below(4); k >= 0; k--)
			x |= (u128)1 << below(128);
		return x;
	case 4:
		return random128() & ones(1 + below(64));
	default:
		return ((u128)1 << below(128)) - below(3);
	}
}

static long special_integers(void)
{
	return 128 * 3 + 8;
}

/*
 * A floating value's bits, IEEE layout with FRACTION fraction bits and
 * EXPONENT exponent bits (x87: 64 bits of significand with the integer bit
 * set for every normal encoding).  I indexes the special values first.
 */
typedef struct {
	int fraction, exponent, x87;
} format;

static const format f16 = { 10, 5, 0 }, bf16 = { 7, 8, 0 }, f32 = { 23, 8, 0 }, f64 = { 52, 11, 0 },
		    f80 = { 63, 15, 1 }, f128 = { 112, 15, 0 };

static u128 encode(format f, int negative, unsigned biased, u128 fraction)
{
	u128 bits;
	biased &= (1u << f.exponent) - 1;
	fraction &= ones(f.fraction);
	if (f.x87) {
		/* the integer bit is set for every normal, infinite or NaN encoding */
		if (biased)
			fraction |= (u128)1 << 63;
		return (u128)negative << 79 | (u128)biased << 64 | fraction;
	}
	bits = (u128)negative << (f.fraction + f.exponent);
	return bits | (u128)biased << f.fraction | fraction;
}

#define SPECIAL_FLOATS 44

static u128 float_input(format f, long i)
{
	unsigned max = (1u << f.exponent) - 1, bias = max >> 1;
	int negative = (int)(next() & 1);
	u128 fraction;
	if (i < SPECIAL_FLOATS) {
		negative = (int)(i & 1);
		switch (i >> 1) {
		case 0: return encode(f, negative, 0, 0);			/* zero */
		case 1: return encode(f, negative, 0, 1);			/* smallest subnormal */
		case 2: return encode(f, negative, 0, ones(f.fraction));	/* largest subnormal */
		case 3: return encode(f, negative, 1, 0);			/* smallest normal */
		case 4: return encode(f, negative, max - 1, ones(f.fraction));	/* largest */
		case 5: return encode(f, negative, max, 0);			/* infinity */
		case 6: return encode(f, negative, max, (u128)1 << (f.fraction - 1));	/* quiet NaN */
		case 7: return encode(f, negative, max, 1);			/* signalling NaN */
		case 8: return encode(f, negative, max, ones(f.fraction));	/* NaN, all payload */
		case 9: return encode(f, negative, max, (u128)0x5a5 << (f.fraction > 20 ? f.fraction - 13 : 0) | 1);
		case 10: return encode(f, negative, bias, 0);			/* 1 */
		case 11: return encode(f, negative, bias, (u128)1 << (f.fraction - 1));	/* 1.5 */
		case 12: return encode(f, negative, bias - 1, ones(f.fraction));	/* just below 1 */
		case 13: return encode(f, negative, bias + 1, 1);		/* just above 2 */
		case 14: return encode(f, negative, bias + 127, 0);		/* 2^127 */
		case 15: return encode(f, negative, bias + 126, ones(f.fraction));
		case 16: return encode(f, negative, bias + 63, 0);		/* 2^63 */
		case 17: return encode(f, negative, bias + 31, 0);		/* 2^31 */
		case 18: return encode(f, negative, bias - 1, 0);		/* 0.5 */
		case 19: return encode(f, negative, max, (u128)1 << (f.fraction - 2));	/* signalling, top payload bit */
		case 20: return encode(f, negative, max, (u128)1 << (f.fraction - 1) | 1);	/* quiet, lowest payload bit */
		default: return encode(f, negative, bias + 128, 0);		/* 2^128 */
		}
	}
	switch (below(8)) {
	case 0:	/* all bits random */
		return encode(f, negative, (unsigned)next() & max, random128());
	case 1:	/* subnormal */
		return encode(f, negative, 0, random128() & ones(1 + below(f.fraction)));
	case 2:	/* near the top of the range */
		return encode(f, negative, max - 1 - below(4), random128());
	case 3:	/* near the bottom of the normals */
		return encode(f, negative, 1 + below(4), random128());
	case 4:	/* a tie or a near-tie for a narrower format: a few high bits, then 1000... or 0111... */
		fraction = random128() << below((unsigned)f.fraction) ;
		fraction &= ones(f.fraction);
		fraction |= (u128)1 << below((unsigned)f.fraction);
		if (next() & 1)
			fraction -= 1;
		return encode(f, negative, bias - 40 + below(80), fraction);
	case 5:	/* within the range of 128-bit integers */
		return encode(f, negative, bias + below(130), random128());
	default:	/* a moderate value */
		return encode(f, negative, bias - 20 + below(40), random128());
	}
}

/* a value just outside the range of the narrower format N, made in format F */
static u128 float_input_for(format f, format narrow, long i)
{
	if (i >= SPECIAL_FLOATS && i % 5 == 0) {
		/* around the narrow format's overflow and subnormal thresholds */
		unsigned bias = ((1u << f.exponent) - 1) >> 1, nbias = ((1u << narrow.exponent) - 1) >> 1;
		unsigned e = (next() & 1) ? bias + nbias + below(2) : bias - nbias - narrow.fraction - 2 + below(narrow.fraction + 4);
		return encode(f, (int)(next() & 1), e, random128() & ~ones(below(f.fraction)));
	}
	return float_input(f, i);
}

/* ---------------- results ---------------- */

static u64 digest;
static long calls;
static const char *verbose;	/* the helper whose calls are printed */
static const char *current;

static void hex(const void *p, int size)
{
	const unsigned char *b = p;
	for (int k = size - 1; k >= 0; k--)
		printf("%02x", b[k]);
}

/* each argument and the result: SIZES of the pieces in order, the last the result */
static void record(int count, const void *const *pieces, const int *sizes)
{
	const unsigned char *result = pieces[count - 1];
	calls++;
	for (int k = 0; k < sizes[count - 1]; k++) {
		digest ^= result[k];
		digest *= 0x100000001b3ull;
	}
	if (verbose && !strcmp(verbose, current)) {
		for (int k = 0; k < count; k++) {
			if (k)
				printf(k == count - 1 ? " -> " : " ");
			hex(pieces[k], sizes[k]);
		}
		printf("\n");
	}
}

#define RECORD1(a, r) record(2, (const void *const[]){ &(a), &(r) }, (const int[]){ sizeof_value(a), sizeof_value(r) })
#define RECORD2(a, b, r) record(3, (const void *const[]){ &(a), &(b), &(r) }, (const int[]){ sizeof_value(a), sizeof_value(b), sizeof_value(r) })
#define RECORD4(a, b, c, d, r) record(5, (const void *const[]){ &(a), &(b), &(c), &(d), &(r) }, \
	(const int[]){ sizeof_value(a), sizeof_value(b), sizeof_value(c), sizeof_value(d), sizeof_value(r) })
/* the bytes that matter: an x87 value is 10 of its 16 */
#define sizeof_value(x) _Generic((x), long double: 10, _Complex long double: 32, default: (int)sizeof(x))

static int begin(const char *name)
{
	current = name;
	digest = 0xcbf29ce484222325ull;
	calls = 0;
	state = 0x9e3779b97f4a7c15ull;	/* the same inputs whatever ran before */
	for (const char *p = name; *p; p++)
		state = state * 31 + (unsigned char)*p;
	return !verbose || !strcmp(verbose, name);
}

static void end(void)
{
	if (!verbose)
		printf("%s %ld %016llx\n", current, calls, digest);
}

/* values from bits */
static float to_f32(u128 b) { unsigned u = (unsigned)b; float x; memcpy(&x, &u, 4); return x; }
static double to_f64(u128 b) { u64 u = (u64)b; double x; memcpy(&x, &u, 8); return x; }
static long double to_f80(u128 b) { long double x = 0; memcpy(&x, &b, 10); return x; }
static __float128 to_f128(u128 b) { __float128 x; memcpy(&x, &b, 16); return x; }
static _Float16 to_f16(u128 b) { unsigned short u = (unsigned short)b; _Float16 x; memcpy(&x, &u, 2); return x; }
static __bf16 to_bf16(u128 b) { unsigned short u = (unsigned short)b; __bf16 x; memcpy(&x, &u, 2); return x; }

/* C defines the conversion of the value of format F with bits B to an integer
   of BITS bits (signed or not): finite, and its truncation representable */
static int fits(format f, u128 b, int bits, int is_signed)
{
	unsigned max = (1u << f.exponent) - 1, bias = max >> 1;
	int total = f.x87 ? 80 : f.fraction + f.exponent + 1;
	unsigned biased = (unsigned)(b >> (f.x87 ? 64 : f.fraction)) & max;
	int negative = (int)(b >> (total - 1)) & 1;
	u128 fraction = b & ones(f.x87 ? 63 : f.fraction);
	if (biased == max)
		return 0;
	if (!is_signed)	/* (-1, 2^bits) */
		return negative ? biased < bias : biased < bias + (unsigned)bits;
	if (biased < bias + (unsigned)bits - 1)	/* (-2^(bits-1), 2^(bits-1)) */
		return 1;
	return negative && biased == bias + (unsigned)bits - 1 && !fraction;	/* exactly -2^(bits-1) */
}

/* ---------------- the tests ---------------- */

static void integer_tests(long n)
{
	long total = special_integers() + n;
#define DIVISION(NAME, T)                                                \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			T a = (T)integer_input(i), b = (T)integer_input(next() % total); \
			T r;                                             \
			if (!b)                                          \
				continue;                                \
			r = NAME(a, b);                                  \
			RECORD2(a, b, r);                                \
		}                                                        \
		end();                                                   \
	}
	DIVISION(__udivti3, u128)
	DIVISION(__umodti3, u128)
	DIVISION(__divti3, i128)
	DIVISION(__modti3, i128)
#define SHIFT(NAME)                                                      \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			i128 a = (i128)integer_input(i), r;              \
			int b = (int)(i < 128 ? i : below(128));         \
			r = NAME(a, b);                                  \
			RECORD2(a, b, r);                                \
		}                                                        \
		end();                                                   \
	}
	SHIFT(__ashlti3)
	SHIFT(__ashrti3)
	SHIFT(__lshrti3)
#define FROM_INTEGER(NAME, T, R)                                         \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			T a = (T)integer_input(i);                       \
			R r = NAME(a);                                   \
			RECORD1(a, r);                                   \
		}                                                        \
		end();                                                   \
	}
	FROM_INTEGER(__floattisf, i128, float)
	FROM_INTEGER(__floattidf, i128, double)
	FROM_INTEGER(__floattixf, i128, long double)
	FROM_INTEGER(__floattitf, i128, __float128)
	FROM_INTEGER(__floatuntisf, u128, float)
	FROM_INTEGER(__floatuntidf, u128, double)
	FROM_INTEGER(__floatuntixf, u128, long double)
	FROM_INTEGER(__floatuntitf, u128, __float128)
	FROM_INTEGER(__floatsitf, int, __float128)
	FROM_INTEGER(__floatditf, long long, __float128)
	FROM_INTEGER(__floatunsitf, unsigned, __float128)
	FROM_INTEGER(__floatunditf, unsigned long long, __float128)
}

static void conversion_tests(long n)
{
	long total = SPECIAL_FLOATS + n;
	/* to an integer: signed only where the result is defined */
#define TO_INTEGER(NAME, F, TO, R, BITS, SIGNED)                         \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			u128 bits = float_input(F, i);                   \
			if (!fits(F, bits, BITS, SIGNED))                \
				continue;                                \
			{                                                \
				__typeof__(TO(bits)) a = TO(bits);       \
				R r = NAME(a);                           \
				RECORD1(a, r);                           \
			}                                                \
		}                                                        \
		end();                                                   \
	}
	TO_INTEGER(__fixsfti, f32, to_f32, i128, 128, 1)
	TO_INTEGER(__fixdfti, f64, to_f64, i128, 128, 1)
	TO_INTEGER(__fixxfti, f80, to_f80, i128, 128, 1)
	TO_INTEGER(__fixunssfti, f32, to_f32, u128, 128, 0)
	TO_INTEGER(__fixunsdfti, f64, to_f64, u128, 128, 0)
	TO_INTEGER(__fixunsxfti, f80, to_f80, u128, 128, 0)
	TO_INTEGER(__fixtfsi, f128, to_f128, int, 32, 1)
	TO_INTEGER(__fixtfdi, f128, to_f128, long long, 64, 1)
	TO_INTEGER(__fixtfti, f128, to_f128, i128, 128, 1)
	TO_INTEGER(__fixunstfsi, f128, to_f128, unsigned, 32, 0)
	TO_INTEGER(__fixunstfdi, f128, to_f128, unsigned long long, 64, 0)
	TO_INTEGER(__fixunstfti, f128, to_f128, u128, 128, 0)
	/* between floating formats; a narrowing gets inputs near the narrow format's limits */
#define CONVERT(NAME, F, TO, NARROW, R)                                  \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			__typeof__(TO(0)) a = TO(float_input_for(F, NARROW, i)); \
			R r = NAME(a);                                   \
			RECORD1(a, r);                                   \
		}                                                        \
		end();                                                   \
	}
	CONVERT(__extendsftf2, f32, to_f32, f32, __float128)
	CONVERT(__extenddftf2, f64, to_f64, f64, __float128)
	CONVERT(__extendxftf2, f80, to_f80, f80, __float128)
	CONVERT(__trunctfsf2, f128, to_f128, f32, float)
	CONVERT(__trunctfdf2, f128, to_f128, f64, double)
	CONVERT(__trunctfxf2, f128, to_f128, f80, long double)
	CONVERT(__extendhfsf2, f16, to_f16, f16, float)
	CONVERT(__extendbfsf2, bf16, to_bf16, bf16, float)
	CONVERT(__truncsfhf2, f32, to_f32, f16, _Float16)
	CONVERT(__truncdfhf2, f64, to_f64, f16, _Float16)
	CONVERT(__truncxfhf2, f80, to_f80, f16, _Float16)
	CONVERT(__trunctfhf2, f128, to_f128, f16, _Float16)
	CONVERT(__truncsfbf2, f32, to_f32, bf16, __bf16)
	CONVERT(__truncdfbf2, f64, to_f64, bf16, __bf16)
#ifndef NO_TRUNCXFBF2
	CONVERT(__truncxfbf2, f80, to_f80, bf16, __bf16)
#endif
	CONVERT(__trunctfbf2, f128, to_f128, bf16, __bf16)
}

/* a second operand related to the first: equal, negated, close or random */
static u128 partner(format f, u128 a, long i)
{
	int total = f.fraction + f.exponent + 1;
	switch (next() % 6) {
	case 0:
		return a;
	case 1:
		return a ^ (u128)1 << (total - 1);
	case 2:	/* a close magnitude, either sign: cancellation */
		return (a ^ (random128() & ones(1 + below(f.fraction)))) ^ ((u128)(next() & 1) << (total - 1));
	case 3:	/* exponent a little apart */
		return a + ((u128)below(130) << f.fraction) * ((next() & 1) ? 1 : (u128)-1);
	default:
		return float_input(f, i < SPECIAL_FLOATS ? (long)(next() % SPECIAL_FLOATS) : i);
	}
}

static void quad_tests(long n)
{
	long total = SPECIAL_FLOATS * SPECIAL_FLOATS + n;
#define BINARY(NAME, R)                                                  \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			u128 x = i < SPECIAL_FLOATS * SPECIAL_FLOATS ? float_input(f128, i / SPECIAL_FLOATS) : float_input(f128, i); \
			u128 y = i < SPECIAL_FLOATS * SPECIAL_FLOATS ? float_input(f128, i % SPECIAL_FLOATS) : partner(f128, x, i); \
			__float128 a = to_f128(x), b = to_f128(y);       \
			R r = NAME(a, b);                                \
			RECORD2(a, b, r);                                \
		}                                                        \
		end();                                                   \
	}
	BINARY(__addtf3, __float128)
	BINARY(__subtf3, __float128)
	BINARY(__multf3, __float128)
	BINARY(__divtf3, __float128)
	BINARY(__eqtf2, int)
	BINARY(__netf2, int)
	BINARY(__lttf2, int)
	BINARY(__letf2, int)
	BINARY(__gttf2, int)
	BINARY(__getf2, int)
	BINARY(__unordtf2, int)
}

/* a complex operand part: special values often, so that the recovery paths run */
static u128 complex_part(format f, long i)
{
	unsigned bias = ((1u << f.exponent) - 1) >> 1;
	switch (below(10)) {
	case 0: case 1: case 2:
		return float_input(f, (long)below(SPECIAL_FLOATS));
	case 3:	/* huge or tiny: the scaling and overflow paths */
		return float_input(f, i);
	default:
		return encode(f, (int)(next() & 1), bias - 30 + below(60), random128());
	}
}

/*
 * The parts of a complex result that are NaNs, made the one NaN: which NaN
 * hardware arithmetic gives for NaN operands depends on the order the
 * compiler put the operations in (IEEE 754 6.2.3 leaves it open), and the
 * Annex G formulas reach such operations; whether a part is a NaN is what
 * is compared.
 */
static void canonical_nans(void *r, format f)
{
	int size = f.x87 ? 16 : (f.fraction + f.exponent + 1) / 8;
	for (int part = 0; part < 2; part++) {
		unsigned char *p = (unsigned char *)r + part * size;
		u128 bits = 0;
		unsigned max = (1u << f.exponent) - 1;
		if (f.x87)	/* the 6 bytes after an x87 value are padding, whatever the callee left */
			memset(p + 10, 0, 6);
		memcpy(&bits, p, f.x87 ? 10 : size);
		if (((unsigned)(bits >> (f.x87 ? 64 : f.fraction)) & max) == max && (bits & ones(f.x87 ? 63 : f.fraction))) {
			bits = encode(f, 0, max, (u128)1 << (f.fraction - 1));
			memcpy(p, &bits, f.x87 ? 10 : size);
		}
	}
}

static void complex_tests(long n)
{
	long total = n + 1000;
#define COMPLEX(NAME, F, TO)                                             \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			__typeof__(TO(0)) a = TO(complex_part(F, i)), b = TO(complex_part(F, i)); \
			__typeof__(TO(0)) c = TO(complex_part(F, i)), d = TO(complex_part(F, i)); \
			__typeof__(NAME(a, b, c, d)) r = NAME(a, b, c, d); \
			canonical_nans(&r, F);                           \
			RECORD4(a, b, c, d, r);                          \
		}                                                        \
		end();                                                   \
	}
	COMPLEX(__mulsc3, f32, to_f32)
	COMPLEX(__muldc3, f64, to_f64)
	COMPLEX(__mulxc3, f80, to_f80)
	COMPLEX(__multc3, f128, to_f128)
	COMPLEX(__divsc3, f32, to_f32)
	COMPLEX(__divdc3, f64, to_f64)
	COMPLEX(__divxc3, f80, to_f80)
	COMPLEX(__divtc3, f128, to_f128)
}

static void power_tests(long n)
{
	long total = n + SPECIAL_FLOATS * 8;
#define POWER(NAME, F, TO)                                               \
	if (begin(#NAME)) {                                              \
		for (long i = 0; i < total; i++) {                       \
			__typeof__(TO(0)) a = TO(i < SPECIAL_FLOATS * 8 ? float_input(F, i / 8) : complex_part(F, i)); \
			static const int fixed[8] = { 0, 1, -1, 2, -2, 31, -31, 0x7fffffff }; \
			int b = i < SPECIAL_FLOATS * 8 ? fixed[i % 8] : (int)(next() % 4 ? (long)below(80) - 40 : (long)(int)next()); \
			__typeof__(TO(0)) r = NAME(a, b);                \
			RECORD2(a, b, r);                                \
		}                                                        \
		end();                                                   \
	}
	POWER(__powisf2, f32, to_f32)
	POWER(__powidf2, f64, to_f64)
	POWER(__powixf2, f80, to_f80)
	POWER(__powitf2, f128, to_f128)
}

int main(int argc, char **argv)
{
	long n = argc > 1 ? atol(argv[1]) : 20000;
	verbose = argc > 2 ? argv[2] : NULL;
	integer_tests(n);
	conversion_tests(n);
	quad_tests(n);
	complex_tests(n / 4);
	power_tests(n / 4);
	return 0;
}
