#define NI __attribute__((noinline))

#define REDUCE(name, count) \
NI unsigned name(const unsigned char *p, unsigned seed) { \
	for (__SIZE_TYPE__ i = 0; i < count; i++) seed |= p[i]; \
	return seed; \
}

REDUCE(one_vector, 16)
REDUCE(with_tail, 148)
REDUCE(short_scan, 15)

NI unsigned volatile_scan(const volatile unsigned char *p, unsigned seed)
{
	for (__SIZE_TYPE__ i = 0; i < 148; i++)
		seed |= p[i];
	return seed;
}

NI unsigned mutating_scan(unsigned char *p)
{
	unsigned bits = 0;
	for (__SIZE_TYPE__ i = 0; i < 148; i++)
		bits |= p[i]++;
	return bits;
}

int main(void)
{
	/* Offset by one. Nonzero neighbours detect rounding the read range. */
	unsigned char data[150] = {255}, *p = data + 1;
	data[149] = 255;
	for (unsigned i = 0; i < 148; i++) {
		unsigned value = 1u << (i & 7), seed = 0x12340000 | (i >> 4);
		p[i] = value;
		if (with_tail(p, seed) != (seed | value) || volatile_scan(p, seed) != (seed | value) ||
		    one_vector(p, seed) != (seed | (i < 16 ? value : 0)) ||
		    short_scan(p, seed) != (seed | (i < 15 ? value : 0)))
			return 1;
		p[i] = 0;
	}
	if (mutating_scan(p) != 0 || mutating_scan(p) != 1)
		return 2;
	return data[0] != 255 || data[149] != 255;
}
