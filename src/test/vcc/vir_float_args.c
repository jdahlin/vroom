/* A float argument through VIR (-O2): its word in the argument slot is the
   float's 4 bytes, read back as f32.  The builder used to store it as an f64
   and then refuse the whole function ("a value of one type read as another"),
   so nothing that called a function with a float parameter went through VIR:
   a double constant converted to float, a float expression, a loaded float, a
   float kept across a loop, mixed with integers and doubles. */
#include <stdio.h>
#define NI __attribute__((noinline))

float g[4] = { 1.5f, -2.25f, 3.0f, 0.125f };

NI float f1(float x) { return x * 2; }
NI double f2(int a, float x, double y, float z) { return a + x + y + z; }
NI float f3(float *p, float s) { return p[0] + s; }

NI float k1(void) { return f1(0.75); }
NI float k2(float a, float b) { return f1(a + b); }
NI float k3(int i) { return f1(g[i]); }
NI double k4(float a) { return f2(3, a, 2.5, g[1]); }
NI float k5(int n)
{
	float s = 0.5f;

	for (int i = 0; i < n; i++)
		s = f3(g + (i & 3), s);
	return s;
}

int main(void)
{
	printf("%g %g %g %g %g\n", k1(), k2(1.25f, 2.5f), k3(2), k4(0.5f), k5(6));
	return 0;
}
