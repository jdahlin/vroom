/* vectors under 8 bytes are returned in v0 the way LLVM legalizes them:
   lanes widened to fill 8 bytes (v4qi as 4h, v2hi and v2qi as 2s), one
   lane as it is (emit/abi.c vec_small). */
#include <stdio.h>

typedef char v4qi __attribute__((vector_size(4)));
typedef short v2hi __attribute__((vector_size(4)));
typedef float v1sf __attribute__((vector_size(4)));
typedef signed char v2qi __attribute__((vector_size(2)));
typedef short v1hi __attribute__((vector_size(2)));
typedef char v1qi __attribute__((vector_size(1)));
v4qi r4(int k);
v2hi rh(int k);
v1sf rf(float x);
v2qi r2(int k);
v1hi r1h(int k);
v1qi r1q(int k);

int main(void)
{
	v4qi a = r4(9);
	v2hi b = rh(300);
	v1sf c = rf(1.5f);
	v2qi d = r2(-5);
	v1hi e = r1h(-1234);
	v1qi f = r1q(77);
	int bad = a[0] != 9 || a[1] != 2 || a[3] != 4 || b[0] != 300 || b[1] != -7 || c[0] != 3.0f ||
		  d[0] != -5 || d[1] != -3 || e[0] != -1234 || f[0] != 77;

	printf("%d %d %d %d %g %d %d %d %d %d\n", a[0], a[3], b[0], b[1], (double)c[0], d[0], d[1], e[0], f[0], bad);
	return bad;
}
