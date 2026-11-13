/* Scalar SysV calls through VIR: integer/SSE register banks overflow independently,
 * indirect and variadic calls, and live values survive caller clobbers. */
#include <stdarg.h>
#define NI __attribute__((noinline))
NI long ints(long a,long b,long c,long d,long e,long f,long g,long h)
{ return a+3*b+5*c+7*d+9*e+11*f+13*g+17*h; }
NI double floats(double a,double b,double c,double d,double e,double f,double g,double h,double i,double j)
{ return a-b+c-d+e-f+g-h+i-j; }
NI double varied(int n,...)
{ va_list ap; va_start(ap,n); double s=0; for(int i=0;i<n;i++)s+=va_arg(ap,double); va_end(ap);return s; }
NI long caller(long x)
{ long y=ints(x,2,3,4,5,6,7,8); return y+ints(8,7,6,5,4,3,2,x)+x; }
NI double indirect(double (*fn)(double,double,double,double,double,double,double,double,double,double),double x)
{ double y=x+0.5; double z=fn(y,2,3,4,5,6,7,8,9,10); return z+y; }
NI double variadic_caller(double x)
{ return varied(10,x,2.,3.,4.,5.,6.,7.,8.,9.,10.)+x; }
int main(void)
{
	if(caller(11)!=(11+6+15+28+45+66+91+136)+(8+21+30+35+36+33+26+187)+11)return 1;
	if(indirect(floats,1.0)!=-3.0)return 2;
	if(variadic_caller(1.0)!=56.0)return 3;
	return 0;
}
