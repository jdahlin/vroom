/* Outlining a once-used helper must remain possible in a large caller. */
#define NI __attribute__((noinline))
#define STEP x=(x*33)^(x>>3)^91UL;
#define FOUR(a) a a a a
#define SIXTEEN(a) FOUR(FOUR(a))
#define SIXTYFOUR(a) FOUR(SIXTEEN(a))
#define MANY(a) SIXTEEN(SIXTYFOUR(a))
static unsigned long helper(unsigned long x)
{
 SIXTYFOUR(STEP)
 return x;
}
NI unsigned long large(unsigned long x)
{
 unsigned long y=helper(x);
 MANY(STEP)
 return x+y;
}
NI unsigned long reference(unsigned long x)
{
 unsigned long y=x;
 for(unsigned j=0;j<64;j++)y=(y*33)^(y>>3)^91UL;
 for(unsigned j=0;j<1024;j++)x=(x*33)^(x>>3)^91UL;
 return x+y;
}
int main(void)
{
 unsigned long a[]={0,1,~0UL,0x6badbeefffffffffUL};
 for(unsigned i=0;i<sizeof a/sizeof *a;i++)
  if(large(a[i])!=reference(a[i]))return 1;
 return 0;
}
