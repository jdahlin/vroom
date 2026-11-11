/* Dense integer-result switches: independent literal-answer oracles. */
#define NI __attribute__((noinline))
#define RETURNS(B) case (B): return 19; case (B)+1: return -7; \
 case (B)+2: case (B)+3: return 33; case (B)+4: return 9; \
 case (B)+6: return 81; case (B)+7: return -12; default: return 101;
#define ASSIGNS(B,V) case (B): V=19; break; case (B)+1: V=7; break; \
 case (B)+2: case (B)+3: V=33; break; case (B)+4: V=9; break; \
 case (B)+6: V=81; break; case (B)+7: V=12; break; default: V=101; break;
NI int sv_return(int x) { switch(x) {RETURNS(-2)} }
NI unsigned sv_assign(unsigned x) {unsigned v=999;switch(x){ASSIGNS(3u,v)}return v;}
NI long long sv_signed64(long long x) {switch(x){RETURNS((-9223372036854775807LL-1LL))}}
NI unsigned long long sv_unsigned64(unsigned long long x) {switch(x){RETURNS(18446744073709551608ULL)}}
NI int sv_signed32(int x) {switch(x){RETURNS((-2147483647-1))}}
NI signed char sv_narrow(int x) {switch(x){RETURNS(0)}}
NI unsigned short sv_short(int x) {switch(x){RETURNS(0)}}
NI long long sv_range(long long x) {switch(x){case -4 ... -2:return 19;
 case -1:return -7;case 0:return 33;case 1:return 9;case 3:return 81;case 4:return -12;default:return 101;}}
NI unsigned sv_twice(unsigned x) {unsigned a,b;switch(x){ASSIGNS(3u,a)}switch(x+1){ASSIGNS(3u,b)}return a+3*b;}
static volatile unsigned effects, observed;
static NI int sv_next(int x) {effects++;return x;}
NI int sv_effect(int x) {switch(sv_next(x)){RETURNS(-2)}}
NI int sv_volatile(void) {switch(observed++){RETURNS(0)}}
static void cleanup(unsigned *x) {effects+=*x;}
NI unsigned sv_cleanup(unsigned x) {unsigned c __attribute__((cleanup(cleanup)))=3;
 switch(x){case 0:return 19;case 1:return 7;case 2:return 33;case 3:return 9;case 4:return 81;case 5:return 12;default:return 101;}}
NI int sv_fallthrough(int x) {int v=4;switch(x){case 0:v=19;case 1:v+=7;break;
 case 2:v=33;break;case 3:v=9;break;case 4:v=81;break;case 5:v=12;break;default:v=101;break;}return v;}
NI int sv_missing(int x) {switch(x){case 0:return 19;case 1:return 7;case 2:return 33;
 case 3:return 9;case 4:return 81;case 5:return 12;}return 55;}
NI int sv_label(int x,int go) {if(go)goto inside;switch(x){case 0:return 19;
 case 1:return 7;case 2:return 33;case 3:inside:return 9;case 4:return 81;case 5:return 12;default:return 101;}}
NI int sv_nested(int x) {switch(x){case 0:return 19;case 1:return 7;case 2:return 33;
 case 3:return 9;case 4:return 81;if(0){case 5:return 12;}default:return 101;}}
NI int sv_sparse(int x) {switch(x){case 0:return 19;case 100:return 7;case 200:return 33;
 case 300:return 9;case 400:return 81;case 500:return 12;default:return 101;}}
static __attribute__((always_inline)) inline int sv_inline(int x) {switch(x){RETURNS(-2)}}
NI int sv_inline_twice(int x) {return sv_inline(x)+sv_inline(x+1);}

int main(void) {
 static const int answers[8]={19,-7,33,33,9,101,81,-12};
 static const unsigned positive[8]={19,7,33,33,9,101,81,12};
 for (int i=-5;i<13;i++) {
  int a=(unsigned)(i+2)<8?answers[i+2]:101;
  unsigned b=(unsigned)(i-3)<8?positive[i-3]:101;
  if(sv_return(i)!=a || sv_assign((unsigned)i)!=b)return 1;
  int nr=(unsigned)i<8?answers[i]:101;
  if(sv_narrow(i)!=(signed char)nr || sv_short(i)!=(unsigned short)nr)return 2;
  int a1=(unsigned)(i+3)<8?answers[i+3]:101;
  if(sv_inline_twice(i)!=a+a1)return 3;
  unsigned b1=(unsigned)(i-2)<8?positive[i-2]:101;
  if(sv_twice((unsigned)i)!=b+3*b1)return 4;
  effects=0;if(sv_effect(i)!=a || effects!=1)return 5;
 }
 for(unsigned i=0;i<10;i++) {
  long long s=(-9223372036854775807LL-1LL)+(long long)i;
  int a=i<8?answers[i]:101;
  if(sv_signed64(s)!=a || sv_signed32((-2147483647-1)+(int)i)!=a)return 6;
  unsigned long long u=18446744073709551608ULL+i;
  if(sv_unsigned64(u)!=(unsigned long long)(i<8?answers[i]:101))return 7;
 }
 if(sv_signed64(9223372036854775807LL)!=101 || sv_signed32(2147483647)!=101)return 8;
 for(int i=-7;i<8;i++) {
  int a=i>=-4&&i<=-2?19:i==-1?-7:i==0?33:i==1?9:i==3?81:i==4?-12:101;
  if(sv_range(i)!=a)return 9;
 }
 observed=2;effects=0;
 if(sv_volatile()!=33 || observed!=3)return 10;
 if(sv_cleanup(4)!=81 || effects!=3)return 11;
 if(sv_fallthrough(0)!=26 || sv_fallthrough(1)!=11 || sv_missing(90)!=55)return 12;
 if(sv_label(99,1)!=9 || sv_label(4,0)!=81 || sv_nested(5)!=12 || sv_sparse(500)!=12)return 13;
 return 0;
}
