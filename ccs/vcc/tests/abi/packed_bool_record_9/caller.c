#include "types.h"
int main(void){struct S x={}; for(int i=0;i<LANES;i++)x.b[i]=i%3==0;struct S y=f(x);for(int i=0;i<LANES;i++)if(y.b[i]!=(i%3==0))return 1;return 0;}
