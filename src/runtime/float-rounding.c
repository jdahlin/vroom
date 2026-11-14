/*
 * Rounding an exactly known value to a binary floating format (to nearest,
 * ties to even, with gradual underflow and overflow to infinity) or to an
 * integer (toward zero, saturating).  Every helper that produces a floating
 * or integer result from a wider intermediate ends here.
 *
 * The value comes as vccrt_unpacked: significand * 2^(exponent - 127) with
 * bit 127 of the significand set, plus a sticky flag for nonzero bits
 * already dropped below it.  The formats keep at most 113 bits, so at least
 * 15 bits are dropped and the round bit is always inside the significand.
 */
#include "runtime-internal.h"

vccrt_rounded __vccrt_round(vccrt_unpacked u, vccrt_format f)
{
	vccrt_rounded r;
	int min_exponent = 1 - f.max_exponent;
	int drop = 128 - f.precision;
	int subnormal = u.exponent < min_exponent;
	int sticky = u.sticky;
	int round_bit;
	vccrt_u128 kept;

	if (subnormal) {
		/* fewer bits are kept: the result's last bit has weight 2^(min_exponent - precision + 1) */
		int extra = min_exponent - u.exponent;
		drop = extra > 128 ? 129 : drop + extra;
	}
	if (drop > 128) {
		kept = 0;
		round_bit = 0;
		sticky = 1;	/* the significand is nonzero */
	} else if (drop == 128) {
		kept = 0;
		round_bit = (int)(u.significand >> 127);
		sticky |= (u.significand << 1) != 0;
	} else {
		kept = u.significand >> drop;
		round_bit = (int)(u.significand >> (drop - 1)) & 1;
		sticky |= (u.significand << (129 - drop)) != 0;
	}
	if (round_bit && (sticky || (kept & 1)))
		kept++;
	if (subnormal) {
		/* rounding up to the smallest normal sets the leading bit */
		r.biased_exponent = (int)(kept >> (f.precision - 1)) & 1;
	} else {
		int exponent = u.exponent;
		if (kept >> f.precision) {	/* carried out: 2^precision */
			kept >>= 1;
			exponent++;
		}
		r.biased_exponent = exponent + f.max_exponent;
		if (r.biased_exponent > 2 * f.max_exponent) {
			r.biased_exponent = 2 * f.max_exponent + 1;
			kept = 0;
		}
	}
	r.significand = kept;
	return r;
}

vccrt_unpacked __vccrt_integer_unpack(int negative, vccrt_u128 magnitude)
{
	vccrt_unpacked u;
	int zeros = vccrt_clz128(magnitude);
	u.negative = negative;
	u.significand = magnitude << zeros;
	u.exponent = 127 - zeros;
	u.sticky = 0;
	return u;
}

/*
 * KIND is what *_unpack returned (0 finite nonzero, 1 zero, 2 infinity,
 * 3 NaN).  The fraction is discarded.  A result out of range saturates:
 * the largest or smallest value for a signed type, by the sign (a NaN by
 * its sign bit too); for an unsigned type every negative value gives 0 and
 * a too large one all ones.  Within range this is C's conversion; outside it
 * C leaves the result undefined and this is the choice the usual runtime
 * makes for a magnitude of 2^BITS or more.
 */
vccrt_u128 __vccrt_integer_from(int kind, const vccrt_unpacked *u, int bits, int is_signed)
{
	vccrt_u128 all_ones = bits == 128 ? ~(vccrt_u128)0 : ((vccrt_u128)1 << bits) - 1;
	vccrt_u128 signed_max = all_ones >> 1;
	vccrt_u128 magnitude;

	if (kind == 1)
		return 0;
	if (!is_signed && u->negative)
		return 0;
	if (kind != 0 || u->exponent >= bits)
		return !is_signed ? all_ones : u->negative ? signed_max + 1 : signed_max;
	if (u->exponent < 0)
		return 0;
	magnitude = u->significand >> (127 - u->exponent);
	if (!is_signed)
		return magnitude;
	magnitude = u->negative ? (vccrt_u128)0 - magnitude : magnitude;
	return magnitude & all_ones;
}
