/* A pointer local whose address an inlined reader advances (sqlite's Utf8Read(&z)): its slot
   holds the pointer and, after each increment, the integer (p2i, add, i2p); promoted under
   VIR's -O2 to one variable.  Also `mask & x` with the constant first. */
typedef unsigned char u8;

static const u8 tab[64] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07 };

static unsigned rd(const u8 **pz)
{
	unsigned c = *((*pz)++);

	if (c >= 0xc0) {
		c = tab[c - 0xc0];
		while ((**pz & 0xc0) == 0x80)
			c = (c << 6) + (0x3f & *((*pz)++));
		if (c < 0x80)
			c = 0xfffd;
	}
	return c;
}

__attribute__((noinline)) static int cmp(const u8 *a, const u8 *b)
{
	unsigned c, d;
	int n = 0;

	while ((c = a[0] < 0x80 ? *(a++) : rd(&a)) != 0) {
		d = rd(&b);
		if (c != d)
			return -1 - n;
		n++;
	}
	return n + (int)(b[0] == 0);
}

int main(void)
{
	static const u8 s1[] = "ab\xc3\xa9z", s2[] = "ab\xc3\xa9z", s3[] = "ab\xc3\xa8z";

	if (rd(&(const u8 *){ (const u8 *)"\xc3\xa9" }) != 0xe9) return 1;
	if (cmp(s1, s2) != 5) return 2;
	if (cmp(s1, s3) != -3) return 3;
	return 0;
}
