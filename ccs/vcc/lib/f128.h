/* IEEE binary128 on the host, as bits in a u128: the long double of aarch64
   Linux, for constants that must be parsed and folded at that precision.
   x86's 80-bit long double (x87 extended: binary128's exponent, 64-bit
   significand) is held in the same layout with the low 49 fraction bits
   zero: the ...p functions round to p fraction bits, 112 or 63 (Q_P80).
   Integer arithmetic only, round to nearest even, no exception flags (the
   tf soft float in tests/rt is the same algorithm).  Header-only: cc/ and
   emit/ each include it. */
#ifndef VCC_F128_H
#define VCC_F128_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef __int128 f128_i;
typedef unsigned __int128 f128_u;
#define u128 f128_u
#define i128 f128_i

#define F128_UNUSED __attribute__((unused))
#define Q_M112 (((u128)1 << 112) - 1)
#define Q_SIGN ((u128)1 << 127)

F128_UNUSED static bool q_isnan(u128 u) { return (u << 1) > ((u128)0x7fff << 113); }
F128_UNUSED static bool q_isinf(u128 u) { return (u << 1) == ((u128)0x7fff << 113); }
F128_UNUSED static bool q_nz(u128 u) { return (u << 1) != 0; }	/* NaN counts as true, as in C */
F128_UNUSED static u128 q_qnan(void) { return (u128)0x7fff8 << 108; }

/* sign, biased exponent and significand m, any width, whose value is
   m * 2^(e - bias - p - 3); rounds to nearest even with m's low bit sticky */
F128_UNUSED static u128 q_round(int s, int e, u128 m, int p, int emax)
{
	int eb = emax == 0x7fff ? 15 : emax == 0x7ff ? 11 : emax == 0x1f ? 5 : 8;
	unsigned g;

	if (!m)
		return (u128)s << (p + eb);
	while (m >> (p + 4)) {
		m = (m >> 1) | (m & 1);
		e++;
	}
	while (!(m >> (p + 3))) {
		m <<= 1;
		e--;
	}
	if (e >= emax)
		return (u128)s << (p + eb) | (u128)emax << p;
	if (e <= 0) {
		int sh = 1 - e;

		m = sh > p + 4 ? 1 : (m >> sh) | ((m & (((u128)1 << sh) - 1)) != 0);
		e = 0;
	}
	g = (unsigned)(m & 7);
	m >>= 3;
	if (g > 4 || (g == 4 && (m & 1)))
		m++;
	if (m >> (p + 1)) {
		m >>= 1;
		e++;
	} else if (!e && (m >> p)) {
		e = 1;	/* a subnormal rounded up to the smallest normal */
	}
	if (e >= emax)
		m = 0, e = emax;
	else if (e)
		m &= ((u128)1 << p) - 1;
	return (u128)s << (p + eb) | (u128)e << p | m;
}

#define Q_P80 63	/* x87 extended: 63 fraction bits after the explicit integer bit */

/* q_round for binary128's exponent at p fraction bits (112 or Q_P80), in
   binary128's layout; the value is m * 2^(e - 16383 - 115) as for p 112 */
F128_UNUSED static u128 q_rndp(int s, int e, u128 m, int p)
{
	u128 r;

	if (p == 112)
		return q_round(s, e, m, 112, 0x7fff);
	r = q_round(s, e - (112 - p), m, p, 0x7fff);
	return (r >> (p + 15)) << 127 | ((r >> p) & 0x7fff) << 112 | (r & (((u128)1 << p) - 1)) << (112 - p);
}

/* exponent (subnormals: 1) and significand with the implicit bit at 112 */
F128_UNUSED static void q_un(u128 u, int *s, int *e, u128 *m)
{
	*s = (int)(u >> 127);
	*e = (int)(u >> 112) & 0x7fff;
	*m = u & Q_M112;
	if (*e)
		*m |= (u128)1 << 112;
	else
		*e = 1;
}

F128_UNUSED static void q_norm(int *e, u128 *m)
{
	while (*m && !(*m >> 112)) {
		*m <<= 1;
		--*e;
	}
}

F128_UNUSED static u128 q_addp(u128 a, u128 b, int p)
{
	int sa, ea, sb, eb, d;
	u128 ma, mb, m;

	if (q_isnan(a) || q_isnan(b))
		return q_qnan();
	if (q_isinf(a))
		return q_isinf(b) && (a ^ b) >> 127 ? q_qnan() : a;
	if (q_isinf(b))
		return b;
	q_un(a, &sa, &ea, &ma);
	q_un(b, &sb, &eb, &mb);
	if (!ma && !mb)
		return (u128)(sa & sb) << 127;
	ma <<= 3, mb <<= 3;
	if (ea < eb || (ea == eb && ma < mb)) {
		u128 t = ma;
		int ti = ea;

		ma = mb, mb = t, ea = eb, eb = ti;
		ti = sa, sa = sb, sb = ti;
	}
	d = ea - eb;
	if (d > 120)
		mb = mb != 0;
	else if (d)
		mb = (mb >> d) | ((mb & (((u128)1 << d) - 1)) != 0);
	m = sa == sb ? ma + mb : ma - mb;
	if (!m)
		return 0;
	return q_rndp(sa, ea, m, p);
}

F128_UNUSED static u128 q_add(u128 a, u128 b) { return q_addp(a, b, 112); }
F128_UNUSED static u128 q_sub(u128 a, u128 b) { return q_addp(a, b ^ Q_SIGN, 112); }
F128_UNUSED static u128 q_subp(u128 a, u128 b, int p) { return q_addp(a, b ^ Q_SIGN, p); }

F128_UNUSED static u128 q_mulp(u128 a, u128 b, int p)
{
	u128 ma, mb, p00, p01, p10, p11, mid, lo, hi;
	int sa, ea, sb, eb, s = (int)((a ^ b) >> 127);
	uint64_t a0, a1, b0, b1;

	if (q_isnan(a) || q_isnan(b))
		return q_qnan();
	q_un(a, &sa, &ea, &ma);
	q_un(b, &sb, &eb, &mb);
	if (q_isinf(a) || q_isinf(b))
		return (!ma || !mb) ? q_qnan() : (u128)s << 127 | (u128)0x7fff << 112;
	if (!ma || !mb)
		return (u128)s << 127;
	q_norm(&ea, &ma);
	q_norm(&eb, &mb);
	a0 = (uint64_t)ma, a1 = (uint64_t)(ma >> 64);
	b0 = (uint64_t)mb, b1 = (uint64_t)(mb >> 64);
	p00 = (u128)a0 * b0, p01 = (u128)a0 * b1, p10 = (u128)a1 * b0, p11 = (u128)a1 * b1;
	mid = p01 + p10;
	hi = p11 + (mid >> 64) + ((u128)(mid < p01) << 64);
	lo = p00 + (mid << 64);
	hi += lo < p00;
	/* the product's leading bit is at 224 or 225: keep 116 bits and a sticky one */
	lo = (hi << 19) | (lo >> 109) | ((lo & (((u128)1 << 109) - 1)) != 0);
	return q_rndp(s, ea + eb - 16383, lo, p);
}

F128_UNUSED static u128 q_mul(u128 a, u128 b) { return q_mulp(a, b, 112); }

F128_UNUSED static u128 q_divp(u128 a, u128 b, int p)
{
	u128 ma, mb, q = 0;
	int sa, ea, sb, eb, s = (int)((a ^ b) >> 127), e;

	if (q_isnan(a) || q_isnan(b))
		return q_qnan();
	q_un(a, &sa, &ea, &ma);
	q_un(b, &sb, &eb, &mb);
	if (q_isinf(a))
		return q_isinf(b) ? q_qnan() : (u128)s << 127 | (u128)0x7fff << 112;
	if (q_isinf(b))
		return (u128)s << 127;
	if (!mb)
		return !ma ? q_qnan() : (u128)s << 127 | (u128)0x7fff << 112;
	if (!ma)
		return (u128)s << 127;
	q_norm(&ea, &ma);
	q_norm(&eb, &mb);
	e = ea - eb + 16383;
	if (ma < mb) {
		ma <<= 1;
		e--;
	}
	for (int i = 0; i < 116; i++) {
		q <<= 1;
		if (ma >= mb) {
			ma -= mb;
			q |= 1;
		}
		ma <<= 1;
	}
	return q_rndp(s, e, q | (ma != 0), p);
}

F128_UNUSED static u128 q_div(u128 a, u128 b) { return q_divp(a, b, 112); }

/* -1, 0, 1; 2 when unordered */
F128_UNUSED static int q_cmp(u128 a, u128 b)
{
	if (q_isnan(a) || q_isnan(b))
		return 2;
	if (!((a | b) << 1))
		return 0;
	if ((a >> 127) != (b >> 127))
		return a >> 127 ? -1 : 1;
	if (a == b)
		return 0;
	return ((a < b) ^ (int)(a >> 127)) ? -1 : 1;
}

/* exact */
F128_UNUSED static u128 q_of_d(double d)
{
	uint64_t u;
	int s, e;
	u128 m;

	memcpy(&u, &d, 8);
	s = (int)(u >> 63), e = (int)(u >> 52) & 0x7ff, m = u & ((1ull << 52) - 1);
	if (e == 0x7ff)
		return (u128)s << 127 | (u128)0x7fff << 112 | m << 60 | (m ? (u128)1 << 111 : 0);
	if (!e) {
		if (!m)
			return (u128)s << 127;
		e = 1;
		while (!(m >> 52)) {
			m <<= 1;
			e--;
		}
		m &= (1ull << 52) - 1;
	}
	return (u128)s << 127 | (u128)(e - 1023 + 16383) << 112 | m << 60;
}

/* rounded to binary64 (p 52) or binary32 (p 23): the bits */
F128_UNUSED static uint64_t q_trunc(u128 a, int p, int emax, int bias)
{
	int s, e, eb = emax == 0x7ff ? 11 : emax == 0x1f ? 5 : 8;
	u128 m;

	q_un(a, &s, &e, &m);
	if (q_isnan(a))
		return (uint64_t)((u128)s << (p + eb) | (u128)emax << p | (u128)1 << (p - 1));
	if (q_isinf(a))
		return (uint64_t)((u128)s << (p + eb) | (u128)emax << p);
	if (!m)
		return (uint64_t)((u128)s << (p + eb));
	q_norm(&e, &m);
	m = (m >> (109 - p)) | ((m & (((u128)1 << (109 - p)) - 1)) != 0);
	return (uint64_t)q_round(s, e - 16383 + bias, m, p, emax);
}

/* bfloat16 storage: float's exponent, seven fraction bits. */
F128_UNUSED static uint16_t q_to_bf(u128 a) { return (uint16_t)q_trunc(a, 7, 0xff, 127); }
F128_UNUSED static double bf_to_d(uint16_t h)
{
	uint32_t u = (uint32_t)h << 16;
	float f;
	memcpy(&f, &u, 4);
	return (double)f;
}

/* IEEE binary16 storage, independent of host half support. */
F128_UNUSED static uint16_t q_to_h(u128 a) { return (uint16_t)q_trunc(a, 10, 0x1f, 15); }

F128_UNUSED static double h_to_d(uint16_t h)
{
	uint64_t sign = (uint64_t)(h >> 15) << 63, m = h & 1023, bits;
	int ex = (h >> 10) & 31;
	double d;

	if (ex == 31)
		bits = sign | (uint64_t)0x7ff << 52 | m << 42;
	else if (!ex && !m)
		bits = sign;
	else {
		if (!ex) {
			ex = 1;
			while (!(m & 1024)) m <<= 1, ex--;
			m &= 1023;
		}
		bits = sign | (uint64_t)(ex - 15 + 1023) << 52 | m << 42;
	}
	memcpy(&d, &bits, 8);
	return d;
}

F128_UNUSED static double q_to_d(u128 a)
{
	uint64_t u = q_trunc(a, 52, 0x7ff, 1023);
	double d;

	memcpy(&d, &u, 8);
	return d;
}

F128_UNUSED static float q_to_f(u128 a)
{
	uint32_t u = (uint32_t)q_trunc(a, 23, 0xff, 127);
	float f;

	memcpy(&f, &u, 4);
	return f;
}

/* an integer, signed or not, rounded */
F128_UNUSED static u128 q_of_ip(i128 v, bool sgn, int p)
{
	bool neg = sgn && v < 0;

	return q_rndp(neg, 16383 + 115, neg ? -(u128)v : (u128)v, p);
}

F128_UNUSED static u128 q_of_i(i128 v, bool sgn) { return q_of_ip(v, sgn, 112); }

/* a value rounded to p fraction bits (binary128 to x87 extended); NaN and
   infinity stay, a NaN's payload is cut */
F128_UNUSED static u128 q_to_p(u128 a, int p)
{
	int s, e;
	u128 m;

	if (p == 112 || q_isinf(a) || !q_nz(a))
		return a;
	if (q_isnan(a))
		return a & ~(((u128)1 << (112 - p)) - 1);
	q_un(a, &s, &e, &m);
	return q_rndp(s, e, m << 3, p);
}

/* the 10 bytes of an x87 extended value held as above (little endian:
   64-bit significand with its explicit integer bit, then sign and exponent) */
F128_UNUSED static void q_to_x87(u128 a, uint8_t out[10])
{
	unsigned e = (unsigned)(a >> 112) & 0x7fff;
	uint64_t m = (uint64_t)(a >> 49) & ((1ull << 63) - 1);
	unsigned se = (unsigned)(a >> 127) << 15 | e;

	if (e)
		m |= 1ull << 63;	/* normal, infinity and NaN: the integer bit set */
	for (int i = 0; i < 8; i++)
		out[i] = (uint8_t)(m >> (8 * i));
	out[8] = (uint8_t)se, out[9] = (uint8_t)(se >> 8);
}

/* truncated toward zero to a bits-wide integer, saturating out of range
   and 0 for NaN (clang's folding of the undefined cases) */
F128_UNUSED static i128 q_to_int(u128 a, int bits, bool sgn)
{
	int s, e;
	u128 m, max = bits == 128 ? ~(u128)0 : ((u128)1 << bits) - 1;

	q_un(a, &s, &e, &m);
	if (q_isnan(a) || e < 16383)
		return 0;
	e -= 16383;
	if (sgn)
		max >>= 1;
	if (s && !sgn)
		return 0;
	if (e >= bits)
		return s ? -(i128)max - 1 : (i128)max;
	m = e >= 112 ? m << (e - 112) : m >> (112 - e);
	if (!s && m > max)
		return (i128)max;
	if (s && m > max + 1)
		return -(i128)max - 1;
	return s ? -(i128)m : (i128)m;
}

/* truncated toward zero, wrapping as the host's conversions do */
F128_UNUSED static i128 q_to_i(u128 a)
{
	int s, e;
	u128 m;

	q_un(a, &s, &e, &m);
	if (q_isnan(a) || e < 16383)
		return 0;
	e -= 16383;
	m = e >= 128 ? ~(u128)0 : e >= 112 ? m << (e - 112) : m >> (112 - e);
	return s ? -(i128)m : (i128)m;
}

/* ---- parsing: decimal (exact, with a bignum) and hexadecimal ---- */

typedef struct qbig { uint32_t *w; int n, cap; } qbig;	/* little-endian limbs */

F128_UNUSED static void qb_mulsmall(qbig *b, uint32_t mul, uint32_t add)
{
	uint64_t carry = add;

	for (int i = 0; i < b->n; i++) {
		uint64_t t = (uint64_t)b->w[i] * mul + carry;

		b->w[i] = (uint32_t)t;
		carry = t >> 32;
	}
	if (carry)
		b->w[b->n++] = (uint32_t)carry;
}

F128_UNUSED static int qb_bits(const qbig *b)
{
	int n = b->n;

	while (n && !b->w[n - 1])
		n--;
	return n ? (n - 1) * 32 + 32 - __builtin_clz(b->w[n - 1]) : 0;
}

F128_UNUSED static bool qb_bit(const qbig *b, int i) { return i / 32 < b->n && (b->w[i / 32] >> (i % 32) & 1); }

/* b <<= k (cap must allow it) */
F128_UNUSED static void qb_shl(qbig *b, int k)
{
	int wsh = k / 32, bsh = k % 32, n = b->n + wsh + 1;

	for (int i = n - 1; i >= 0; i--) {
		uint32_t hi = i - wsh >= 0 && i - wsh < b->n ? b->w[i - wsh] : 0;
		uint32_t lo = i - wsh - 1 >= 0 && i - wsh - 1 < b->n ? b->w[i - wsh - 1] : 0;

		b->w[i] = bsh ? hi << bsh | lo >> (32 - bsh) : hi;
	}
	b->n = n;
	while (b->n && !b->w[b->n - 1])
		b->n--;
}

/* r >= d << k ? r -= d << k, true */
F128_UNUSED static bool qb_subshl(qbig *r, const qbig *d, int k)
{
	int wsh = k / 32, bsh = k % 32, n = d->n + wsh + 1;
	int64_t borrow = 0;

	if (qb_bits(r) < qb_bits(d) + k)
		return false;
	/* compare from the top */
	for (int i = (r->n > n ? r->n : n) - 1; i >= 0; i--) {
		uint32_t hi = i - wsh >= 0 && i - wsh < d->n ? d->w[i - wsh] : 0;
		uint32_t lo = i - wsh - 1 >= 0 && i - wsh - 1 < d->n ? d->w[i - wsh - 1] : 0;
		uint32_t dv = bsh ? hi << bsh | lo >> (32 - bsh) : hi, rv = i < r->n ? r->w[i] : 0;

		if (rv != dv) {
			if (rv < dv)
				return false;
			break;
		}
	}
	for (int i = 0; i < r->n; i++) {
		uint32_t hi = i - wsh >= 0 && i - wsh < d->n ? d->w[i - wsh] : 0;
		uint32_t lo = i - wsh - 1 >= 0 && i - wsh - 1 < d->n ? d->w[i - wsh - 1] : 0;
		uint32_t dv = i < n ? (bsh ? hi << bsh | lo >> (32 - bsh) : hi) : 0;
		int64_t t = (int64_t)r->w[i] - dv - borrow;

		borrow = t < 0;
		r->w[i] = (uint32_t)t;
	}
	while (r->n && !r->w[r->n - 1])
		r->n--;
	return true;
}

/* the 128 bits of b from bit lo up, and whether any bit below lo is set */
F128_UNUSED static u128 qb_top(const qbig *b, int lo, bool *sticky)
{
	u128 m = 0;

	for (int i = 127; i >= 0; i--)
		m = m << 1 | qb_bit(b, lo + i);
	*sticky = false;
	for (int i = 0; i < lo && !*sticky; i++)
		*sticky = qb_bit(b, i);
	return m;
}

/* a decimal or hexadecimal floating constant (no suffix), correctly rounded
   to p fraction bits */
F128_UNUSED static u128 q_parsep(const char *s, size_t n, int p)
{
	size_t i = 0;
	int s0 = 0;

	if (i < n && (s[i] == '+' || s[i] == '-'))
		s0 = s[i++] == '-';
	if (n - i > 1 && s[i] == '0' && (s[i + 1] | 32) == 'x') {
		u128 m = 0;
		long be = 0, ex = 0;
		bool sticky = false, frac = false, eneg = false;

		for (i += 2; i < n; i++) {
			int d;

			if (s[i] == '.') {
				frac = true;
				continue;
			}
			if (s[i] >= '0' && s[i] <= '9')
				d = s[i] - '0';
			else if ((s[i] | 32) >= 'a' && (s[i] | 32) <= 'f')
				d = (s[i] | 32) - 'a' + 10;
			else
				break;
			if (!(m >> 120)) {
				m = m << 4 | (unsigned)d;
				if (frac)
					be -= 4;
			} else {
				sticky |= d != 0;
				if (!frac)
					be += 4;
			}
		}
		if (i < n && (s[i] | 32) == 'p') {
			i++;
			if (i < n && (s[i] == '+' || s[i] == '-'))
				eneg = s[i++] == '-';
			for (; i < n && s[i] >= '0' && s[i] <= '9'; i++)
				if (ex < 100000)
					ex = ex * 10 + (s[i] - '0');
		}
		be += eneg ? -ex : ex;
		if (!m)
			return (u128)s0 << 127;
		if (be > 40000)
			be = 40000;
		if (be < -40000)
			be = -40000;
		if (sticky) {	/* keep the sticky bit below the 116 that are rounded */
			m = m >> 1 | 1;
			be++;
		}
		return q_rndp(s0, (int)(16383 + 115 + be), m, p);
	}
	{
		/* digits D and exponent E: value D * 10^E */
		long e10 = 0, ex = 0, nd = 0;
		bool eneg = false, frac = false;
		size_t st = i, en;
		qbig b, d;
		u128 m, res;
		int k = 0;
		bool sticky;

		for (en = i; en < n && ((s[en] >= '0' && s[en] <= '9') || s[en] == '.'); en++)
			if (s[en] != '.')
				nd++;
		i = en;
		if (i < n && (s[i] | 32) == 'e') {
			i++;
			if (i < n && (s[i] == '+' || s[i] == '-'))
				eneg = s[i++] == '-';
			for (; i < n && s[i] >= '0' && s[i] <= '9'; i++)
				if (ex < 100000)
					ex = ex * 10 + (s[i] - '0');
		}
		/* limbs: the digits, 10^|E| and the shift below, with room */
		b.cap = (int)(nd / 9 + 2 + (5000 + 40000 / 9) / 9 * 2 + 600);
		b.w = calloc((size_t)b.cap, 4);
		b.n = 0;
		for (size_t j = st; j < en; j++) {
			if (s[j] == '.') {
				frac = true;
				continue;
			}
			if (frac)
				e10--;
			qb_mulsmall(&b, 10, (uint32_t)(s[j] - '0'));
		}
		e10 += eneg ? -ex : ex;
		if (!qb_bits(&b)) {
			free(b.w);
			return (u128)s0 << 127;
		}
		/* out of range either way: the value has fewer than nd + E digits before the point */
		if ((long)nd + e10 > 4934 + 1) {
			free(b.w);
			return (u128)s0 << 127 | (u128)0x7fff << 112;
		}
		if ((long)nd + e10 < -4967 - 1) {
			free(b.w);
			return (u128)s0 << 127;
		}
		if (e10 >= 0) {
			int bits;

			b.w = realloc(b.w, (size_t)(b.cap + e10 / 9 + 4) * 4);
			b.cap += (int)(e10 / 9 + 4);
			for (long j = 0; j < e10; j++)
				qb_mulsmall(&b, 10, 0);
			bits = qb_bits(&b);
			k = bits > 128 ? bits - 128 : 0;
			m = qb_top(&b, k, &sticky);
			free(b.w);
			if (sticky)	/* 128 bits leave 12 below the rounding point: room for a sticky one */
				m |= 1;
			return q_rndp(s0, 16383 + 115 + k, m, p);
		}
		/* D / 10^-E: shift D so that the quotient has 120 bits */
		d.cap = (int)(-e10 / 9 + 8);
		d.w = calloc((size_t)d.cap, 4);
		d.w[0] = 1, d.n = 1;
		for (long j = 0; j < -e10; j++)
			qb_mulsmall(&d, 10, 0);
		k = qb_bits(&d) - qb_bits(&b) + 120;
		if (k < 0) {	/* D is the longer one: shift 10^-E up instead */
			d.w = realloc(d.w, (size_t)(d.n + -k / 32 + 4) * 4);
			d.cap = d.n + -k / 32 + 4;
			qb_shl(&d, -k);
		} else {
			if (b.cap < b.n + k / 32 + 2) {
				b.w = realloc(b.w, (size_t)(b.n + k / 32 + 4) * 4);
				b.cap = b.n + k / 32 + 4;
			}
			qb_shl(&b, k);
		}
		m = 0;
		for (int j = qb_bits(&b) - qb_bits(&d); j >= 0; j--)
			if (qb_subshl(&b, &d, j))
				m |= (u128)1 << j;
		res = q_rndp(s0, 16383 + 115 - k, m | (qb_bits(&b) != 0), p);
		free(b.w);
		free(d.w);
		return res;
	}
}

F128_UNUSED static u128 q_parse(const char *s, size_t n) { return q_parsep(s, n, 112); }

#undef u128
#undef i128
#endif
