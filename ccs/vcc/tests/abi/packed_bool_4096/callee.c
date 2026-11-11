#include "types.h"
B4096 flip4096(B4096 a,int m,B4096 b) { B4096 r=a^b; if(m!=19)r[0]=!r[0]; return r; }
B4096 pressure4096(long a,long b,long c,long d,long e,long f,long g,long h,B4096 x) { if(a+b+c+d+e+f+g+h!=36)x[0]=!x[0]; return x; }
