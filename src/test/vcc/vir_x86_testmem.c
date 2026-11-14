#define NI __attribute__((noinline))
NI int flag8(const unsigned char *p) {return (*p&0x80u)!=0;}
NI int flag16(const unsigned short *p) {return (*p&0x9000u)==0;}
NI int flag32(const unsigned *p) {return (*p&0x80000001u)!=0;}
NI int flag64(const unsigned long long *p) {return (*p&0xffffffff80000000ULL)==0;}
NI int indexed(const unsigned short *p,long long i) {return (p[i]&0x9000u)!=0;}
NI int branch8(const unsigned char *p, volatile int *taken) {
 if (*p & 0x80u) ++*taken;
 return *taken;
}
NI int vol(const volatile unsigned char *p) {return (*p&0x80u)!=0;}
NI int sign(const signed char *p) {return (*p&0x100)!=0;}
NI unsigned shared(const unsigned char *p) {unsigned x=*p;return (x&0x80)?x:0;}
NI unsigned sharedmask(const unsigned char *p) {unsigned x=*p&0x80;return (x!=0)+x;}
struct record {unsigned pad[9];unsigned short flags;};
NI int member(const struct record *p) {return (p->flags&0x9000u)!=0;}
NI long long keep(const unsigned char *p,long long x,long long y) {int c=(*p&0x80)!=0;return (c?x:y)+c;}
int main(void) {
 for(unsigned i=0;i<256;i++) {
  unsigned char a=i;unsigned short b=i*257;unsigned c=i*0x01010101u;
  unsigned long long d=((unsigned long long)c<<32)|c;
  volatile int taken=7;
  if(branch8(&a,&taken)!=7+(i>=128)) return 8;
  if(flag8(&a)!=(int)(i>=128)||vol(&a)!=(int)(i>=128))return 1;
  if(flag16(&b)!=((b&0x9000u)==0)||indexed(&b,0)!=!!(b&0x9000u))return 2;
  if(flag32(&c)!=!!(c&0x80000001u)||flag64(&d)!=!(d&0xffffffff80000000ULL))return 3;
  if(sign((signed char *)&a)!=(int)(i>=128))return 4;
  if(shared(&a)!=(i>=128?i:0)||sharedmask(&a)!=(i>=128?129u:0u))return 5;
  struct record r={{0},b};if(member(&r)!=!!(b&0x9000u))return 6;
  if(keep(&a,71,-18)!=(i>=128?72:-18))return 7;
 }
 return 0;
}
