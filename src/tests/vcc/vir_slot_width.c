/* A narrow store may retain an i64 operand after truncation folding.
 * Inlined pointer arguments first hide the slot from promotion; the later
 * pass must normalize its stores by the field width, including loop phis. */
#define NI __attribute__((noinline))
#define AI __attribute__((always_inline)) inline
static AI void seed32(unsigned *p, unsigned long v) { *p=(unsigned)v; }
static AI void seed16(unsigned short *p, unsigned long v) { *p=(unsigned short)v; }
static AI void seed8(unsigned char *p, unsigned long v) { *p=(unsigned char)v; }
NI unsigned long mix32(int k, unsigned long v)
{
 unsigned x=0;
 seed32(&x,v);
 if(k) x=(unsigned)k;
 return x;
}
NI long signed32(int k, unsigned long v)
{
 unsigned x=0;
 seed32(&x,v);
 if(k) x=(unsigned)k;
 return (int)x;
}
NI unsigned long mix16(int k, unsigned long v)
{
 unsigned short x=0;
 seed16(&x,v);
 if(k) x=(unsigned short)k;
 return x;
}
NI long mix8(int k, unsigned long v)
{
 unsigned char x=0;
 seed8(&x,v);
 if(k) x=(unsigned char)k;
 return (signed char)x;
}
NI unsigned long loop32(unsigned n, unsigned long v)
{
 unsigned x=0;
 seed32(&x,v);
 for(unsigned i=0;i<n;i++) {
  if(i&1) seed32(&x,(unsigned long)x*33+0x100000007UL);
  else x^=i+17;
 }
 return x;
}
int main(void)
{
 unsigned long a[]={0,1,0x7fffffffUL,0x80000000UL,0xffffffffUL,
                    0x1234567887654321UL,~0UL};
 int k[]={0,1,-1,-128,32768,(-2147483647-1)};
 for(unsigned i=0;i<sizeof a/sizeof *a;i++) {
  for(unsigned j=0;j<sizeof k/sizeof *k;j++) {
   unsigned long x=k[j]?(unsigned)k[j]:a[i]&0xffffffffUL;
   if(mix32(k[j],a[i])!=x || signed32(k[j],a[i])!=(long)(int)x ||
      mix16(k[j],a[i])!=(x&65535) || mix8(k[j],a[i])!=(long)(signed char)x)
    return 1;
  }
  unsigned x=(unsigned)a[i];
  for(unsigned n=0;n<40;n++) {
   if(loop32(n,a[i])!=(unsigned long)x)return 2;
   if(n&1)x=x*33+7;
   else x^=n+17;
  }
 }
 return 0;
}
