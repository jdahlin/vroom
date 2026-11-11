#include "types.h"
B flip(B a,int m,B b){ B r={.v=a.v^b.v}; if(m!=19)r.v[0]=!r.v[0]; return r; }
B pressure(long a,long b,long c,long d,long e,long f,long g,long h,B x){
 if(a+b+c+d+e+f+g+h!=36)x.v[0]=!x.v[0];return x;}
