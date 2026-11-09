/*
 * The AdvSIMD intrinsics PostgreSQL uses (port/simd.h,
 * pg_popcount_aarch64.c), from vcc's own arm_neon.h, each checked against
 * the same computation done one lane at a time.
 */
#include <arm_neon.h>
#include <stdio.h>
#include <stdlib.h>

static int bad;

#define EQ(what, got, want) do { unsigned long long g_ = (got), w_ = (want); \
	if (g_ != w_) { printf("%s: %llx, want %llx\n", what, g_, w_); bad++; } } while (0)

int main(void)
{
	unsigned char b[32];
	unsigned int w[4] = { 1, 0xffffffffu, 7, 1u << 31 }, w2[4] = { 1, 2, 7, 0 };

	for (int i = 0; i < 32; i++)
		b[i] = (unsigned char)(i * 37 + 11);
	uint8x16_t x = vld1q_u8(b), y = vld1q_u8(b + 16);
	uint32x4_t p = vld1q_u32(w), q = vld1q_u32(w2);
	uint8x16_t qs = vqsubq_u8(x, y), mn = vminq_u8(x, y), mx = vmaxq_u8(x, y), z = vzip1q_u8(x, y),
		   cn = vcntq_u8(x), an = vandq_u8(x, y), orr = vorrq_u8(x, y), eq = vceqq_u8(x, y);
	unsigned maxv = 0, cnt8 = 0, sum16 = 0;

	for (int i = 0; i < 16; i++) {
		unsigned a = b[i], c = b[16 + i];

		EQ("vqsubq_u8", qs[i], a > c ? a - c : 0);
		EQ("vminq_u8", mn[i], a < c ? a : c);
		EQ("vmaxq_u8", mx[i], a > c ? a : c);
		EQ("vzip1q_u8", z[i], i & 1 ? b[16 + i / 2] : b[i / 2]);
		EQ("vcntq_u8", cn[i], __builtin_popcount(a));
		EQ("vandq_u8", an[i], a & c);
		EQ("vorrq_u8", orr[i], a | c);
		EQ("vceqq_u8", eq[i], a == c ? 0xff : 0);
		if (a > maxv)
			maxv = a;
		if (i < 8)
			cnt8 += __builtin_popcount(a);
	}
	EQ("vmaxvq_u8", vmaxvq_u8(x), maxv);
	EQ("vaddv_u8(vcnt_u8)", vaddv_u8(vcnt_u8(vld1_u8(b))), cnt8);

	uint32x4_t e = vceqq_u32(p, q), o = vorrq_u32(p, q), d = vdupq_n_u32(0xdeadbeef);
	for (int i = 0; i < 4; i++) {
		EQ("vceqq_u32", e[i], w[i] == w2[i] ? 0xffffffffu : 0);
		EQ("vorrq_u32", o[i], w[i] | w2[i]);
		EQ("vdupq_n_u32", d[i], 0xdeadbeef);
	}
	EQ("vmaxvq_u32", vmaxvq_u32(p), 0xffffffffu);

	uint16x8_t h = vpaddlq_u8(x);
	uint32x4_t s = vpaddlq_u16(h);
	uint64x2_t acc = { 5, 1ull << 40 }, acc0 = acc;
	for (int i = 0; i < 8; i++) {
		EQ("vpaddlq_u8", h[i], b[2 * i] + b[2 * i + 1]);
		sum16 += h[i];
	}
	for (int i = 0; i < 4; i++)
		EQ("vpaddlq_u16", s[i], h[2 * i] + h[2 * i + 1]);
	EQ("vaddvq_u16", vaddvq_u16(h), sum16 & 0xffff);
	acc = vpadalq_u32(acc, s);
	EQ("vpadalq_u32[0]", acc[0], acc0[0] + s[0] + s[1]);
	EQ("vpadalq_u32[1]", acc[1], acc0[1] + s[2] + s[3]);
	acc = vaddq_u64(acc, acc);
	EQ("vaddvq_u64", vaddvq_u64(acc), acc[0] + acc[1]);
	printf("%u %u %llu %d\n", maxv, sum16, (unsigned long long)vaddvq_u64(acc), bad);
	if (bad)
		abort();
	return 0;
}
