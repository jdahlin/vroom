/* A zero-extended byte or word load compared with another such value or with an immediate
 * (x86-64 compares the memory operand at its width, with the unsigned condition): every
 * relation, at the edges of the range, and the signed loads that must stay signed. */
#include <stdio.h>

static __attribute__((noinline)) int ub(unsigned char *p, unsigned char *q, int op)
{
	switch (op) {
	case 0: return *p < *q;
	case 1: return *p <= *q;
	case 2: return *p > *q;
	case 3: return *p >= *q;
	case 4: return *p == *q;
	default: return *p != *q;
	}
}

static __attribute__((noinline)) int uw(unsigned short *p, int op)
{
	switch (op) {
	case 0: return *p < 300;
	case 1: return *p <= 65535;
	case 2: return *p > 255;
	case 3: return *p >= 256;
	default: return *p != 0;
	}
}

static __attribute__((noinline)) int mask(unsigned char *p, int x)
{
	return *p > (x & 127);
}

static __attribute__((noinline)) int sb(signed char *p, signed char *q)
{
	return *p < *q;
}

static __attribute__((noinline)) int sw(short *p, int k)
{
	return *p > k;
}

int main(void)
{
	static const unsigned char v[] = {0, 1, 127, 128, 200, 255};
	static const unsigned short w[] = {0, 1, 255, 256, 299, 300, 32767, 32768, 65535};
	int sum = 0;

	for (int i = 0; i < 6; i++)
		for (int j = 0; j < 6; j++) {
			for (int op = 0; op < 6; op++)
				sum = sum * 3 + ub((unsigned char *)&v[i], (unsigned char *)&v[j], op);
			sum = sum * 3 + mask((unsigned char *)&v[i], v[j]);
			sum = sum * 3 + sb((signed char *)&v[i], (signed char *)&v[j]);
		}
	for (int i = 0; i < 9; i++) {
		for (int op = 0; op < 5; op++)
			sum = sum * 3 + uw((unsigned short *)&w[i], op);
		sum = sum * 3 + sw((short *)&w[i], -5);
	}
	printf("%d\n", sum);
	return 0;
}
