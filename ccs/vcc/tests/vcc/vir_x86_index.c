/* Scaled addresses, negative indices, narrow signed loads and volatile stores. */
#define NI __attribute__((noinline))
NI long load8(const long *p,long i) {return p[i];}
NI int load4(const int *p,long i) {return p[i];}
NI int load2(const short *p,long i) {return p[i];}
NI int load1(const signed char *p,long i) {return p[i];}
NI void store8(volatile long *p,long i,long x) {p[i]=x;}
NI void store4(volatile int *p,long i,int x) {p[i]=x;}
int main(void) {
 long a[9];int b[9];short c[9];signed char d[9];
 for(int j=0;j<9;j++){a[j]=10000000000L-j;b[j]=-5000-j;c[j]=-300-j;d[j]=-100-j;}
 for(long i=-4;i<=4;i++) {
  int j=(int)i+4;
  if(load8(a+4,i)!=a[j]||load4(b+4,i)!=b[j]||load2(c+4,i)!=c[j]||load1(d+4,i)!=d[j])return 1;
  store8(a+4,i,-12345678900L+i);store4(b+4,i,-9000+(int)i);
  if(a[j]!=-12345678900L+i||b[j]!=-9000+(int)i)return 2;
 }
 return 0;
}
