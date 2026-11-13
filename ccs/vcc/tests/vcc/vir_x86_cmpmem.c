/* Exact-width comparisons in memory, including promoted narrow values. */
#define NI __attribute__((noinline))
NI int left(const long *p,long x) {return *p<x;}
NI int right(const long *p,long x) {return x<*p;}
NI int full32(const unsigned *p) {return *p==0x89abcdefU;}
NI int sign8(const signed char *p) {return *p==-128;}
NI int zero8(const unsigned char *p) {return *p==0;}
NI int sign16(const short *p) {return *p==-32768;}
NI int indexed(const long *p,long n,long x) {return p[n]!=x;}
NI int keep(const long *p,long x) {long y=*p;return (y<x)+(y==0);}
NI int vol(const volatile long *p) {return *p==7;}
NI int outside(const unsigned char *p) {return *p==256;}
NI int narrow(const unsigned char *p,int x) {return *p<x;}
int main(void) {
 long a[]={(-9223372036854775807L-1),-32768,-128,-1,0,7,127,255,32767,9223372036854775807L};
 for(unsigned i=0;i<sizeof a/sizeof *a;i++)for(unsigned j=0;j<sizeof a/sizeof *a;j++) {
  if(left(a+i,a[j])!=(a[i]<a[j]) || right(a+i,a[j])!=(a[j]<a[i]))return 1;
  if(indexed(a+5,(long)i-5,a[j])!=(a[i]!=a[j]))return 2;
  if(keep(a+i,a[j])!=((a[i]<a[j])+(a[i]==0)) || vol(a+i)!=(a[i]==7))return 3;
 }
 for(unsigned v=0;v<65536;v++) {
  signed char c=(signed char)v;unsigned char u=(unsigned char)v;short h=(short)v;
  unsigned w=v==43210?0x89abcdefU:v;
  if(sign8(&c)!=(c==-128) || zero8(&u)!=(u==0) || sign16(&h)!=(h==-32768))return 4;
  if(full32(&w)!=(w==0x89abcdefU) || outside(&u))return 5;
  if(narrow(&u,(int)v-256)!=(u<(int)v-256))return 6;
 }
 return 0;
}
