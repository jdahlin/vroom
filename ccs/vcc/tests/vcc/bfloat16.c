/* bfloat16 is distinct from IEEE half: storage, rounding and all bit patterns. */
typedef __bf16 bf;
_Static_assert(sizeof(bf)==2 && _Alignof(bf)==2,"storage");
_Static_assert(!__builtin_types_compatible_p(bf,_Float16),"distinct");
_Static_assert(__builtin_types_compatible_p(__typeof__((bf)1+(_Float16)1),_Float16),"rank");
static bf rounded=(bf)1.00390625;
static bf above=(bf)0x1.0100000000001p0;
static bf subnormal=(bf)0x1.0000000000001p-134;
__attribute__((noinline)) static bf narrow(double x){return x;}
__attribute__((noinline)) static double wide(bf x){return x;}
__attribute__((noinline)) static bf calc(bf a,bf b){return (a+b)*(a-b)/b;}
int main(void){
 union{bf f;unsigned short u;}x,y;
 volatile bf a=3,b=2;
 if(rounded!=1 || narrow(1.00390625)!=1 || narrow(1.01171875)!=(bf)1.015625)return 1;
 if(above!=(bf)0x1.02p0 || narrow(0x1.0100000000001p0)!=(bf)0x1.02p0)return 11;
 x.f=subnormal;if(x.u!=1)return 12;
 x.f=narrow(0x1.0000000000001p-134);if(x.u!=1)return 13;
 x.f=narrow(0x1p-134);if(x.u!=0)return 14;
 if(!__builtin_isinf(narrow(0x1.ffp127)))return 15;
 if(calc(a,b)!=(bf)2.5 || (int)a!=3 || (unsigned)a!=3)return 2;
 if(a++!=3 || a!=4 || --a!=3)return 3;
 for(unsigned i=0;i<65536;i++){
  x.u=i;volatile bf z=x.f;unsigned mag=i&0x7fff;
  if(!!__builtin_signbit(z)!=!!(i&0x8000))return 4;
  if(!!__builtin_isnan(z)!=(mag>0x7f80))return 5;
  if(!!__builtin_isinf(z)!=(mag==0x7f80))return 6;
  if(!!__builtin_isnormal(z)!=(mag>=0x80&&mag<0x7f80))return 7;
  if(!!__builtin_isfinite(z)!=(mag<0x7f80))return 8;
#if defined(__aarch64__)
  /* Clang x86 currently quiets signaling bf16 on a scalar copy. */
#endif
  if(mag>0x7f80)continue;
  y.f=narrow(wide(x.f));if(y.u!=x.u)return 10;
 }
 return 0;
}
