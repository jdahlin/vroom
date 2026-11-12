/* vcc-xfail-wasm: TODO.md wasm: a signed 32-bit constant widened to 64 bits is not sign-extended (main's check 12). */
/* Bytes assembled into a value (VIR at -O2, the record clean-up): 2, 4 or 8 byte
   loads from adjacent offsets or'ed together in big-endian order become one load
   and rev/rev16, in little-endian order one load.  Not when a store comes between
   (it may write those bytes), for volatile bytes, a signed byte not at the top, a
   byte used twice, gaps, or three bytes.  A signed top byte: the value sign-extended
   (sqlite's TWO_BYTE_INT, FOUR_BYTE_INT).  Unaligned addresses and offsets are fine. */
#define NI __attribute__((noinline))
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned u32;
typedef unsigned long long u64;

NI u32 be2(const u8 *p) { return p[0] << 8 | p[1]; }
NI u32 le2(const u8 *p) { return p[0] | p[1] << 8; }
NI u32 be4(const u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
NI u32 le4(const u8 *p) { return p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24; }
NI u32 be4mix(const u8 *p) { return p[3] | (u32)p[0] << 24 | p[2] << 8 | p[1] << 16; }	/* any order */
NI u64 be8(const u8 *p)
{
	return (u64)p[0] << 56 | (u64)p[1] << 48 | (u64)p[2] << 40 | (u64)p[3] << 32 | (u64)p[4] << 24 |
	       (u64)p[5] << 16 | (u64)p[6] << 8 | p[7];
}
NI u64 le8(const u8 *p)
{
	return p[0] | (u64)p[1] << 8 | (u64)p[2] << 16 | (u64)p[3] << 24 | (u64)p[4] << 32 | (u64)p[5] << 40 |
	       (u64)p[6] << 48 | (u64)p[7] << 56;
}
NI u64 be4x(const u8 *p) { return (u64)((u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]); }	/* into an x register */
NI u32 be4off(const u8 *p) { return (u32)p[-3] << 24 | p[-2] << 16 | p[-1] << 8 | p[0]; }

/* a store between the loads: maybe into those bytes */
NI u32 stored(u8 *p, u8 *q) { u32 a = (u32)p[0] << 24 | p[1] << 16; *q = 0x77; return a | p[2] << 8 | p[3]; }
NI u32 vol(const volatile u8 *p) { return (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
typedef signed char i8;
NI int sgn(const i8 *p) { return p[0] << 8 | (u8)p[1]; }	/* the top byte sign-extended: sxth */
NI int sgn4(const u8 *p) { return 16777216 * (i8)p[0] | p[1] << 16 | p[2] << 8 | p[3]; }	/* fills w */
NI long long sgn4x(const u8 *p) { return (long long)(i8)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }	/* sxtw */
NI int sgnle(const u8 *p) { return p[0] | (i8)p[1] << 8; }
NI int sgnlow(const u8 *p) { return p[0] << 8 | (i8)p[1]; }	/* a low signed byte: no */
NI long long sgn8(const u8 *p)
{
	return (long long)(i8)p[0] << 56 | (long long)p[1] << 48 | (long long)p[2] << 40 | (long long)p[3] << 32 |
	       (long long)p[4] << 24 | p[5] << 16 | p[6] << 8 | p[7];
}
NI u32 twice(const u8 *p) { return p[0] << 8 | p[0]; }
NI u32 gap(const u8 *p) { return p[0] << 8 | p[2]; }
NI u32 three(const u8 *p) { return p[0] << 16 | p[1] << 8 | p[2]; }
NI u32 shifted(const u8 *p) { return p[0] << 16 | p[1] << 8; }	/* not at bit 0 */
NI u32 base2(const u8 *p, const u8 *q) { return p[0] << 8 | q[1]; }	/* two bases */
/* the base itself changes between the loads */
NI u32 moved(const u8 *p) { u32 a = p[0] << 8; p += 2; return a | p[-1]; }

/* sqlite's varint-style header read in a loop */
NI u32 sum(const u8 *p, int n)
{
	u32 s = 0;

	for (int i = 0; i < n; i++, p += 4)
		s += (u32)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3];
	return s;
}

int main(void)
{
	static u8 buf[32] = { 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc, 0xde, 0xf0, 0x11, 0x22, 0x33, 0x44, 0x80, 0xff, 0x01, 0x02 };
	u8 w[4] = { 1, 2, 3, 4 };
	volatile u8 v[4] = { 0xa, 0xb, 0xc, 0xd };

	if (be2(buf) != 0x1234 || le2(buf + 1) != 0x5634 || be4(buf + 1) != 0x3456789a) return 1;
	if (le4(buf + 3) != 0xdebc9a78 || be4mix(buf) != 0x12345678) return 2;
	if (be8(buf + 1) != 0x3456789abcdef011ull || le8(buf + 5) != 0x8044332211f0debcull) return 3;
	if (be4x(buf + 12) != 0x80ff0102ull || be4off(buf + 15) != 0x80ff0102u) return 4;
	if (stored(w, w + 2) != 0x01027704u || stored(w, buf + 20) != 0x01027704u) return 5;
	if (vol(v) != 0x0a0b0c0du) return 6;
	if (sgn((const i8 *)buf + 12) != (int)0xffff80ff || twice(buf) != 0x1212) return 7;
	if (sgn((const i8 *)buf) != 0x1234 || sgn4(buf + 12) != (int)0x80ff0102 || sgn4(buf) != 0x12345678) return 11;
	if (sgn4x(buf + 12) != (long long)(int)0x80ff0102 || sgn4x(buf + 1) != 0x3456789a) return 12;
	if (sgnle(buf + 12) != (int)0xffffff80 || sgnle(buf + 11) != (int)0xffff8044 || sgnle(buf) != 0x3412) return 13;
	if (sgnlow(buf + 12) != (int)0xffffffff || sgnlow(buf) != 0x1234) return 14;
	if (sgn8(buf + 12) != (long long)0x80ff010200000000ull || sgn8(buf) != 0x123456789abcdef0ll) return 15;
	if (gap(buf) != 0x1256 || three(buf) != 0x123456 || shifted(buf) != 0x123400) return 8;
	if (base2(buf, buf + 4) != 0x12bc || moved(buf) != 0x1234) return 9;
	if (sum(buf, 3) != 0x12345678u + 0x9abcdef0u + 0x11223344u) return 10;
	return 0;
}
