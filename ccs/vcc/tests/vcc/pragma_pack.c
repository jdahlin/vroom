/* #pragma pack with identifiers and values (TODO.md tier 1, "#pragma pack
   (push, ident, N)").  Layout checked against clang at run time so every
   case reports, not only the first static assertion. */
/* vcc-xfail: #pragma pack(push, ident, N) ignores N; pop, ident pops one */
#include <stddef.h>
#include <stdio.h>

#pragma pack(push, outer, 1)
struct A { char c; int i; };
#pragma pack(push, 2)
struct B { char c; int i; };
#pragma pack(push, inner)
#pragma pack(4)
struct C { char c; long l; };
#pragma pack(pop, outer)		/* pops inner, 2 and outer */
struct D { char c; int i; };
#pragma pack(push, 1)
#pragma pack(push, 8)
#pragma pack(pop)
struct E { char c; int i; };
#pragma pack()
struct F { char c; int i; };

int main(void)
{
	printf("A %zu %zu\n", sizeof(struct A), offsetof(struct A, i));
	printf("B %zu %zu\n", sizeof(struct B), offsetof(struct B, i));
	printf("C %zu %zu\n", sizeof(struct C), offsetof(struct C, l));
	printf("D %zu %zu\n", sizeof(struct D), offsetof(struct D, i));
	printf("E %zu %zu\n", sizeof(struct E), offsetof(struct E, i));
	printf("F %zu %zu\n", sizeof(struct F), offsetof(struct F, i));
	return 0;
}
