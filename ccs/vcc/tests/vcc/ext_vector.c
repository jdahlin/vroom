/* clang's ext_vector_type: N elements, size rounded up to a power of 2. */
#include <stdio.h>

typedef float f2 __attribute__((ext_vector_type(2)));
typedef float f3 __attribute__((ext_vector_type(3)));
typedef int i4 __attribute__((ext_vector_type(4)));
typedef unsigned char c3 __attribute__((ext_vector_type(3)));

struct S { char c; f3 v; c3 w; };

__attribute__((noinline)) f2 add(f2 a, f2 b) { return a + b * 2; }
__attribute__((noinline)) i4 mix(i4 a, int k) { return (a ^ k) - 1; }

int main(void)
{
	f2 a = { 1.5f, -2 }, b = { 0.25f, 3 };
	f2 r = add(a, b);
	f3 t = { 1, 2, 3 };
	i4 m = mix((i4){ 1, 2, 3, 4 }, 6);
	c3 u = { 250, 7, 9 };

	t = t * t;
	u = u + u;
	printf("%zu %zu %zu %zu %zu\n", sizeof(f2), sizeof(f3), _Alignof(f3), sizeof(c3), sizeof(struct S));
	printf("%g %g | %g %g %g | %d %d %d %d | %d %d %d\n", r[0], r[1], t[0], t[1], t[2],
	       m[0], m[1], m[2], m[3], u[0], u[1], u[2]);
	return 0;
}
