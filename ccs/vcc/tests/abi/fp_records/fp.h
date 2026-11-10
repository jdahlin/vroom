/* floating point across compilers: float, double, long double scalars and
   records of them (alone and mixed with integers), results, more vector
   arguments than registers, varargs */
#include <stdarg.h>
struct f2 { float a, b; };
struct f3 { float a, b, c; };
struct d2 { double a, b; };
struct di { double d; long i; };
struct id { int i; float f; double d; };
struct fi3 { float f; int i; char c; };
struct d3 { double a, b, c; };	/* memory */

float rf(float, double);
double rd(int, double, float);
long double rld(long double, int, long double);
struct f2 rf2(float);
struct f3 rf3(float);
struct d2 rd2(double);
struct di rdi(double, long);
struct id rid(int);
struct fi3 rfi3(float);
struct d3 rd3(double);
double many(double, double, double, double, double, double, double, double, double, float, double, struct d2, struct f3, int);
double mixed(struct di, struct id, struct fi3, struct f2, long double, struct d3);
double vsum(int n, ...);	/* tags: 0 double, 1 int, 2 struct d2, 3 struct di, 4 long double, 5 struct f3 */
double back(double (*)(struct d2, float, long double), double);
