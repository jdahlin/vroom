/* register variables of a leaf function live in caller-saved registers (x15 down);
   a function that calls, has inline asm or runs out of temporaries must still be right */
typedef unsigned long u64;
typedef unsigned char u8;

__attribute__((noinline)) static u64 leaf(const u8 *p, int n)
{
	u64 key = 0;
	int i, j = 0, k = 1, m = 7;
	unsigned a = 3, b = 5;

	for (i = 0; i < n; i++) {
		key = (key << 7) | (p[i] & 0x7f);
		j += i * k;
		k ^= (int)key;
		m = m * 3 + (int)(j % 11);
		a += b; b ^= a;
		if (!(p[i] & 0x80))
			break;
	}
	return key + (u64)j + (u64)k + (u64)m + a + b + (u64)i;
}

struct big { long v[9]; };
__attribute__((noinline)) static long calls_later(struct big *d, const struct big *s, int n)
{
	long a = 1, b = 2, c = 3, e = 4, f = 5, t = 0;

	for (int i = 0; i < n; i++) {
		a += i; b ^= a; c += b; e -= c; f += e;
		*d = *s;	/* a memcpy: not a leaf */
		t += d->v[i % 9];
	}
	return a + b + c + e + f + t;
}

__attribute__((noinline)) static long with_asm(long x)
{
	long a = x, b = 2, c = 3, d = 4, e = 5;

	for (int i = 0; i < 5; i++) {
		a += i; b ^= a; c += b; d -= c; e += d;
		__asm__ volatile("mov x9, #77\n\tmov x10, #78" ::: "x9", "x10", "memory");
	}
	return a + b + c + d + e;
}

__attribute__((noinline)) static long many(long x)	/* more live values than registers */
{
	long a = x, b = x + 1, c = x + 2, d = x + 3, e = x + 4, f = x + 5, g = x + 6, h = x + 7;

	for (int i = 0; i < 4; i++) {
		a += b * c; b += c ^ d; c += d - e; d += e * f; e += f ^ g; f += g - h; g += h * a; h += a ^ b;
	}
	return a ^ b ^ c ^ d ^ e ^ f ^ g ^ h;
}

int main(void)
{
	static const u8 in[] = { 0x81, 0x92, 0xa3, 0x74, 0x55, 0x66 };
	struct big d, s;
	long r = 0;

	for (int i = 0; i < 9; i++)
		s.v[i] = i * 11, d.v[i] = 0;
	r += (long)leaf(in, 6) + calls_later(&d, &s, 12) + with_asm(9) + many(3);
	__builtin_printf("%ld\n", r);
	return 0;
}
