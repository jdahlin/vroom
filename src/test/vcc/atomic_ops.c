/* _Atomic objects: every operator on every kind of scalar, with the values
   pre/postfix and compound assignments yield (TODO.md tier 1, "_Atomic
   dropped").  One core under qemu: this checks the results; that the
   accesses are ldar/stlr/casal is checked on the object (TODO.md T6).
   Not here: a signed operand whose compound operation leaves int
   arithmetic (x /= -2 on _Atomic signed char -2, x += 2.75 on _Atomic int
   -4): clang 21 and 23 compute with the old value zero-extended (-127, -2)
   where C and vcc give 1, -1. */
#include <stdio.h>

static int bad;
#define CHECK(c) do { if (!(c)) { printf("line %d: %s\n", __LINE__, #c); bad++; } } while (0)

enum color { RED, GREEN, BLUE };
struct rec { char tag; _Atomic short s; _Atomic(long long) l; };

_Atomic signed char gsc = -128;
_Atomic unsigned char guc = 255;
_Atomic short gs = 32767;
_Atomic unsigned short gus;
_Atomic int gi;
_Atomic unsigned gu;
_Atomic long long gl;
_Atomic unsigned long long gull;
_Atomic _Bool gb;
_Atomic float gf = 1.5f;
_Atomic double gd = 2.25;
_Atomic enum color gc = GREEN;
int arr[8];
int *_Atomic gp = arr;		/* an atomic pointer */
_Atomic(int *) gq = &arr[4];	/* the same type, other spelling */
struct rec grec;

_Static_assert(sizeof(struct rec) == 16 && _Alignof(struct rec) == 8, "rec layout");
_Static_assert(__builtin_offsetof(struct rec, s) == 2 && __builtin_offsetof(struct rec, l) == 8, "rec offsets");
_Static_assert(sizeof(_Atomic(char *)) == 8 && sizeof(_Atomic(struct rec)) == 16, "pointer, record");

__attribute__((noinline)) static int three(void) { return 3; }

__attribute__((noinline)) static void through(_Atomic int *p, _Atomic long long *q)
{
	CHECK(++*p == 1);
	CHECK((*p)++ == 1);
	CHECK(*p == 2);
	*p += three();
	CHECK(*p == 5);
	CHECK((*q -= 7) == -7);
}

int main(void)
{
	/* narrow types wrap as their own type */
	CHECK(gsc-- == -128 && gsc == 127);
	CHECK(++guc == 0);
	CHECK((gs += 1) == -32768);
	CHECK(gus-- == 0 && gus == 65535);
	CHECK((gus >>= 4) == 4095);
	CHECK((guc -= 1) == 255);
	CHECK((gsc *= 2) == -2);

	/* every compound operator on int */
	gi = 100;
	CHECK((gi += 5) == 105);
	CHECK((gi -= 10) == 95);
	CHECK((gi *= 3) == 285);
	CHECK((gi /= 4) == 71);
	CHECK((gi %= 10) == 1);
	CHECK((gi <<= 4) == 16);
	CHECK((gi |= 3) == 19);
	CHECK((gi &= 6) == 2);
	CHECK((gi ^= 7) == 5);
	CHECK((gi >>= 1) == 2);
	CHECK(gi++ == 2 && gi == 3);
	CHECK(--gi == 2);
	gi = -8;
	CHECK((gi >>= 1) == -4);
	gu = 0;
	CHECK(--gu == 0xffffffffu);
	CHECK((gu >>= 28) == 15);
	gl = 1LL << 40;
	CHECK((gl += gl) == 1LL << 41);
	gull = ~0ull;
	CHECK(++gull == 0);
	CHECK((gull -= 1) == ~0ull);
	gi = 4;
	CHECK((gi += 2.75) == 6);	/* computed in double, converted back */

	/* _Bool */
	CHECK(gb++ == 0 && gb == 1);
	CHECK(gb++ == 1 && gb == 1);
	CHECK(--gb == 0);
	CHECK(gb-- == 0 && gb == 1);
	gb = 0;
	CHECK((gb += 2) == 1);

	/* floating point */
	CHECK((gf += 1.0f) == 2.5f);
	CHECK(gf++ == 2.5f && gf == 3.5f);
	CHECK((gf *= 2) == 7.0f);
	CHECK((gd -= 0.25) == 2.0);
	CHECK(--gd == 1.0);
	CHECK((gd /= 4) == 0.25);
	gf = gd;
	CHECK(gf == 0.25f);

	/* enum */
	CHECK(gc++ == GREEN && gc == BLUE);
	CHECK((gc -= 2) == RED);

	/* pointers */
	CHECK(gp++ == arr && gp == &arr[1]);
	CHECK((gp = gp + 3) == &arr[4]);	/* clang rejects += on an atomic pointer */
	CHECK(gp == gq);
	CHECK(--gq == &arr[3]);
	CHECK(gq - arr == 3);
	*gq = 42;
	CHECK(arr[3] == 42);

	/* through pointers, members, locals */
	{
		_Atomic int li = 0;
		_Atomic long long ll = 0;
		struct rec lr = { 1, 2, 3 };
		const _Atomic int ci = 9;

		through(&li, &ll);
		CHECK(li == 5 && ll == -7);
		grec.s = 32767;
		CHECK(++grec.s == -32768);
		CHECK((grec.l += 10) == 10);
		CHECK(lr.s-- == 2 && lr.s == 1);
		CHECK((lr.l <<= 2) == 12);
		CHECK(ci + 1 == 10);
		CHECK(sizeof(gi + 0) == 4 && sizeof(gsc + 0) == 4);
		CHECK(_Generic(gi, int: 1, default: 0));
	}
	printf("%d %u %lld %g %d\n", gi, gu, gl, gd, bad);
	return bad;
}
