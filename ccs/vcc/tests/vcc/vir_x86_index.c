/* Scaled addresses, negative indices, narrow signed loads and volatile stores. */
#define NI __attribute__((noinline))
NI long load8(const long *p,long i) {return p[i];}
NI int load4(const int *p,long i) {return p[i];}
NI int load2(const short *p,long i) {return p[i];}
NI int load1(const signed char *p,long i) {return p[i];}
NI void store8(volatile long *p,long i,long x) {p[i]=x;}
NI void store4(volatile int *p,long i,int x) {p[i]=x;}
NI long cast8(const long *p,long i) {return *(const long *)((unsigned long)p+i*sizeof(long));}
NI int cast4(const int *p,long i) {return *(const int *)((unsigned long)p+i*sizeof(int));}
NI int cast2(const short *p,long i) {return *(const short *)((unsigned long)p+i*sizeof(short));}
NI void caststore4(volatile int *p,long i,int x) {*(volatile int *)((unsigned long)p+i*sizeof(int))=x;}
NI long castmod(const long *p,unsigned key,unsigned n) {
 return *(const long *)((unsigned long)p+(key%n)*sizeof(long));
}
NI unsigned char cast3(const unsigned char *p,long i) {return *(const unsigned char *)((unsigned long)p+i*3);}
NI long sharedcast(const long *p,long i,long *scaled) {
 long j=i*(long)sizeof(long);*scaled=j;return *(const long *)((unsigned long)p+j);
}
int main(void) {
 long a[9];int b[9];short c[9];signed char d[9];
 for(int j=0;j<9;j++){a[j]=10000000000L-j;b[j]=-5000-j;c[j]=-300-j;d[j]=-100-j;}
 for(long i=-4;i<=4;i++) {
  int j=(int)i+4;
  if(load8(a+4,i)!=a[j]||load4(b+4,i)!=b[j]||load2(c+4,i)!=c[j]||load1(d+4,i)!=d[j])return 1;
  if(cast8(a+4,i)!=a[j] || cast4(b+4,i)!=b[j] || cast2(c+4,i)!=c[j])return 3;
  long scaled;
  if(sharedcast(a+4,i,&scaled)!=a[j] || scaled!=i*(long)sizeof(long))return 4;
  caststore4(b+4,i,-(int)i-100);
  if(b[j]!=-(int)i-100)return 5;
  store8(a+4,i,-12345678900L+i);store4(b+4,i,-9000+(int)i);
  if(a[j]!=-12345678900L+i||b[j]!=-9000+(int)i)return 2;
 }
 for(unsigned k=0;k<100;k++)if(castmod(a,k,9)!=a[k%9])return 6;
 unsigned char bytes[27];for(unsigned j=0;j<27;j++)bytes[j]=(unsigned char)(j*37);
 for(long i=-4;i<=4;i++)if(cast3(bytes+12,i)!=bytes[12+i*3])return 7;
 return 0;
}
