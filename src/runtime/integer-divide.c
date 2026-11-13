/*
 * 128-bit integer division and remainder, libgcc's "Integer library
 * routines":
 *
 *   __udivti3(a, b)  a / b, unsigned        __umodti3(a, b)  a % b, unsigned
 *   __divti3(a, b)   a / b, signed          __modti3(a, b)   a % b, signed
 *
 * The signed forms truncate toward zero (C's rule): the quotient is
 * negative when the signs differ, the remainder has the sign of a.  Division
 * by zero divides by zero in the CPU, so it traps as a 64-bit division by
 * zero would (the C behaviour is undefined); the most negative value divided
 * by -1 wraps.
 *
 * The work is on 64-bit halves and 32-bit digits only: a compiler lowers a
 * 128-bit division to these very functions.  A divisor below 2^32 is one
 * digit (short division, four 64-by-32 steps); a longer one uses Knuth's
 * Algorithm D (TAOCP vol. 2, 4.3.1) on base-2^32 digits: normalise so the
 * divisor's top digit has its high bit set, estimate each quotient digit
 * from the top two digits of the remainder and the top divisor digit,
 * correct the estimate with the second divisor digit, multiply and subtract,
 * and add back in the rare case the estimate was still one too large.
 */
#include "runtime-internal.h"

static void udivmod_digits(vccrt_u32 *quotient, vccrt_u32 *remainder, const vccrt_u32 *dividend,
			   const vccrt_u32 *divisor, int divisor_digits)
{
	vccrt_u32 u[5], v[4];
	int n = divisor_digits, shift = __builtin_clz(divisor[n - 1]), i, j;

	/* normalise: divisor << shift, dividend << shift with one more digit */
	for (i = n - 1; i > 0; i--)
		v[i] = shift ? divisor[i] << shift | divisor[i - 1] >> (32 - shift) : divisor[i];
	v[0] = divisor[0] << shift;
	u[4] = shift ? dividend[3] >> (32 - shift) : 0;
	for (i = 3; i > 0; i--)
		u[i] = shift ? dividend[i] << shift | dividend[i - 1] >> (32 - shift) : dividend[i];
	u[0] = dividend[0] << shift;

	for (i = 0; i < 4; i++)
		quotient[i] = 0;
	for (j = 4 - n; j >= 0; j--) {
		vccrt_u64 top = (vccrt_u64)u[j + n] << 32 | u[j + n - 1];
		vccrt_u64 estimate = top / v[n - 1];
		vccrt_u64 rest = top - estimate * v[n - 1];
		vccrt_u64 carry = 0, borrow = 0, difference;

		while (estimate >> 32 || estimate * v[n - 2] > (rest << 32 | u[j + n - 2])) {
			estimate--;
			rest += v[n - 1];
			if (rest >> 32)
				break;
		}
		for (i = 0; i < n; i++) {
			vccrt_u64 product = estimate * v[i] + carry;
			carry = product >> 32;
			difference = (vccrt_u64)u[i + j] - (product & 0xffffffffu) - borrow;
			u[i + j] = (vccrt_u32)difference;
			borrow = difference >> 63;
		}
		difference = (vccrt_u64)u[j + n] - carry - borrow;
		u[j + n] = (vccrt_u32)difference;
		if (difference >> 63) {	/* one too many: add the divisor back */
			vccrt_u64 sum = 0;
			estimate--;
			for (i = 0; i < n; i++) {
				sum = (vccrt_u64)u[i + j] + v[i] + (sum >> 32);
				u[i + j] = (vccrt_u32)sum;
			}
			u[j + n] += (vccrt_u32)(sum >> 32);
		}
		quotient[j] = (vccrt_u32)estimate;
	}
	/* the remainder is u's low n digits, shifted back */
	for (i = 0; i < 4; i++)
		remainder[i] = 0;
	for (i = 0; i < n; i++)
		remainder[i] = shift ? u[i] >> shift | u[i + 1] << (32 - shift) : u[i];
}

static vccrt_u128 udivmod(vccrt_u128 a, vccrt_u128 b, vccrt_u128 *remainder)
{
	vccrt_words x = { .all = a }, y = { .all = b }, q, r;
	vccrt_u32 dividend[4], divisor[4], quotient[4], rest[4];
	int i, digits;

	if (!x.half.high && !y.half.high) {
		if (!y.half.low) {
			volatile vccrt_u64 zero = 0;	/* trap as the CPU does */
			x.half.low /= zero;
		}
		r.half.high = q.half.high = 0;
		q.half.low = x.half.low / y.half.low;
		r.half.low = x.half.low % y.half.low;
		*remainder = r.all;
		return q.all;
	}
	for (i = 0; i < 2; i++) {
		vccrt_u64 xs = i ? x.half.high : x.half.low, ys = i ? y.half.high : y.half.low;
		dividend[2 * i] = (vccrt_u32)xs;
		dividend[2 * i + 1] = (vccrt_u32)(xs >> 32);
		divisor[2 * i] = (vccrt_u32)ys;
		divisor[2 * i + 1] = (vccrt_u32)(ys >> 32);
	}
	for (digits = 4; digits > 1 && !divisor[digits - 1]; digits--)
		;
	if (digits == 1) {
		/* short division by one digit */
		vccrt_u64 carry = 0;
		for (i = 3; i >= 0; i--) {
			vccrt_u64 current = carry << 32 | dividend[i];
			quotient[i] = (vccrt_u32)(current / divisor[0]);
			carry = current % divisor[0];
		}
		rest[0] = (vccrt_u32)carry;
		rest[1] = rest[2] = rest[3] = 0;
	} else {
		udivmod_digits(quotient, rest, dividend, divisor, digits);
	}
	q.half.low = (vccrt_u64)quotient[1] << 32 | quotient[0];
	q.half.high = (vccrt_u64)quotient[3] << 32 | quotient[2];
	r.half.low = (vccrt_u64)rest[1] << 32 | rest[0];
	r.half.high = (vccrt_u64)rest[3] << 32 | rest[2];
	*remainder = r.all;
	return q.all;
}

/* the magnitude of a signed value, as unsigned (the most negative is its own) */
static vccrt_u128 magnitude(vccrt_i128 a)
{
	vccrt_words w = { .signed_all = a };
	if ((vccrt_i64)w.half.high < 0) {
		w.half.low = ~w.half.low + 1;
		w.half.high = ~w.half.high + (w.half.low == 0);
	}
	return w.all;
}

static vccrt_u128 negate(vccrt_u128 a)
{
	vccrt_words w = { .all = a };
	w.half.low = ~w.half.low + 1;
	w.half.high = ~w.half.high + (w.half.low == 0);
	return w.all;
}

static int is_negative(vccrt_i128 a)
{
	vccrt_words w = { .signed_all = a };
	return (vccrt_i64)w.half.high < 0;
}

vccrt_u128 __udivti3(vccrt_u128 a, vccrt_u128 b)
{
	vccrt_u128 r;
	return udivmod(a, b, &r);
}

vccrt_u128 __umodti3(vccrt_u128 a, vccrt_u128 b)
{
	vccrt_u128 r;
	udivmod(a, b, &r);
	return r;
}

vccrt_i128 __divti3(vccrt_i128 a, vccrt_i128 b)
{
	vccrt_u128 r, q = udivmod(magnitude(a), magnitude(b), &r);
	return (vccrt_i128)(is_negative(a) != is_negative(b) ? negate(q) : q);
}

vccrt_i128 __modti3(vccrt_i128 a, vccrt_i128 b)
{
	vccrt_u128 r;
	udivmod(magnitude(a), magnitude(b), &r);
	return (vccrt_i128)(is_negative(a) ? negate(r) : r);
}
