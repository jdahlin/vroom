/* Padding bits are not value bits. Compound loads must normalize before
 * arithmetic, including values supplied through an external ABI or bytes. */
static int addresses, operands;
static int amount(void) { operands++; return 3; }
#define CASE(N) \
typedef unsigned _BitInt(N) U##N; typedef _BitInt(N) S##N; \
static U##N *up##N(U##N *p) { addresses++;return p; } \
static S##N *sp##N(S##N *p) { addresses++;return p; } \
static int test##N(void) { \
 U##N u; S##N s; unsigned char *p=(unsigned char *)&u; \
 for(unsigned i=0;i<sizeof u;i++)p[i]=255; \
 addresses=operands=0; \
 U##N result=(*up##N(&u)>>=amount()); \
 if(result!=((U##N)-1>>3)||u!=result||addresses!=1||operands!=1)return 1; \
 p=(unsigned char *)&s; \
 for(unsigned i=0;i<sizeof s;i++)p[i]=i<N/8?255:0; \
 if(N%8)p[N/8]=(1u<<(N%8))-1; \
 addresses=operands=0; \
 if((*sp##N(&s)>>=amount())!=-1||s!=-1||addresses!=1||operands!=1)return 2; \
 for(unsigned i=0;i<sizeof s;i++)p[i]=i<N/8?0:255; \
 if(N%8)p[N/8]=(unsigned char)~((1u<<(N%8))-1); \
 p[(N-2)/8]|=1u<<((N-2)%8); \
 addresses=operands=0; \
 if((*sp##N(&s)>>=amount())!=(S##N)((U##N)1<<(N-5))||addresses!=1||operands!=1)return 3; \
 p=(unsigned char *)&u; \
 for(unsigned i=0;i<sizeof u;i++)p[i]=255; \
 if((u/=3)!=(U##N)-1/3)return 4; \
 for(unsigned i=0;i<sizeof u;i++)p[i]=255; \
 if((u%=3)!=(U##N)-1%3)return 5; \
 return 0; \
}
CASE(9) CASE(17) CASE(33) CASE(65) CASE(100) CASE(127) CASE(128)
int main(void) {
 if(test9())return 10+test9();
 if(test17())return 20+test17();
 if(test33())return 30+test33();
 if(test65())return 40+test65();
 if(test100())return 50+test100();
 if(test127())return 60+test127();
 if(test128())return 70+test128();
 return 0;
}
