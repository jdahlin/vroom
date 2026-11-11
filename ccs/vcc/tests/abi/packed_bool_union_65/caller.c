#include "types.h"
int main(void){B a={},b={};for(int i=0;i<LANES;i++){a.v[i]=i%3==0;b.v[i]=i%2==0;}
 B r=flip(a,19,b);for(int i=0;i<LANES;i++)if(r.v[i]!=((i%3==0)^(i%2==0)))return 1;
 r=pressure(1,2,3,4,5,6,7,8,a);for(int i=0;i<LANES;i++)if(r.v[i]!=(i%3==0))return 2;
 return 0;}
