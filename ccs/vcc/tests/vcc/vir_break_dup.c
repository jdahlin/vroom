/* VIR lowering: the block every case of an interpreter's switch breaks to (one increment,
 * then the jump back to the dispatch) is copied into the cases.  Cases that break,
 * continue, fall through, return, and change the stepped variable themselves. */
#include <stdio.h>
#define NI __attribute__((noinline))

struct op { unsigned char code; signed char a, b; };

NI static long run(const struct op *p, long *reg, long limit)
{
	long steps = 0, acc = 0;

	for (;; p++) {
		if (++steps > limit)
			return -steps;
		switch (p->code) {
		case 0: return acc + steps * 1000;
		case 1: reg[p->a] += p->b; break;
		case 2: reg[p->a] -= reg[p->b]; break;
		case 3: acc += reg[p->a]; break;
		case 4: if (reg[p->a] > 0) { p += p->b - 1; continue; } break;
		case 5: reg[p->a] = reg[p->b] * 3;
			/* fall through */
		case 6: acc ^= reg[p->a]; break;
		case 7: p += p->b; continue;
		case 8: if (reg[p->a] & 1) break; reg[p->a] >>= 1; acc++; break;
		case 9: p++; acc += p->a; break;
		default: acc = -acc; break;
		}
	}
}

int main(void)
{
	static const struct op prog[] = {
		{ 1, 0, 9 }, { 1, 1, 1 }, { 3, 0, 0 }, { 2, 0, 1 }, { 5, 2, 0 }, { 8, 2, 0 }, { 9, 0, 0 }, { 7, 5, 0 },
		{ 4, 0, -6 }, { 6, 1, 0 }, { 12, 0, 0 }, { 0, 0, 0 },
	};
	long reg[3] = { 0, 0, 0 };

	printf("%ld\n", run(prog, reg, 1000));
	printf("%ld %ld %ld\n", reg[0], reg[1], reg[2]);
	reg[0] = reg[1] = reg[2] = 0;
	printf("%ld\n", run(prog, reg, 17));
	return 0;
}
