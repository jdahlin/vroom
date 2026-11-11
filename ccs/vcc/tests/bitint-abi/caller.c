#include "types.h"
static void fill(Big *v,unsigned seed) {
    unsigned char *p=(void*)v;
    for(unsigned i=0;i<sizeof(*v);i++)p[i]=(unsigned char)(seed+17*i);
    /* Initialize the complete representation, including sign padding. */
    for(unsigned i=(WIDTH+7)/8;i<sizeof(*v);i++)p[i]=0;
    if(WIDTH%8)p[WIDTH/8]&=(1u<<(WIDTH%8))-1;
#if SIGNED
    p[(WIDTH-1)/8]|=(unsigned char)(0xffu<<((WIDTH-1)%8));
    for(unsigned i=(WIDTH+7)/8;i<sizeof(*v);i++)p[i]=255;
#endif
}
static int same(const Big *a,const Big *b) {
    const unsigned char *p=(const void*)a,*q=(const void*)b;
    for(unsigned i=0;i<WIDTH/8;i++)if(p[i]!=q[i])return 0;
    if(WIDTH%8 && ((p[WIDTH/8]^q[WIDTH/8])&((1u<<(WIDTH%8))-1)))return 0;
    return 1;
}
int main(void) {
    Big a,b,expect,r;fill(&a,3);fill(&b,7);
    expect=a;((unsigned char*)&expect)[0]^=((unsigned char*)&b)[0];
    ((unsigned char*)&expect)[1]^=19;
    Big (*volatile fp)(Big,long,Big)=relay;
    r=fp(a,19,b);if(!same(&r,&expect))return 1;
    r=variadic(2,a,17L,b);if(!same(&r,&expect))return 2;
    r=variadic_stack(1,2,3,4,5,6,7,8,a,-17L,b);if(!same(&r,&expect))return 7;
    expect=a;((unsigned char*)&expect)[0]^=41;
    r=pressure(1,2,3,4,5,6,7,8,a,5);if(!same(&r,&expect))return 3;
    Wrapped w={.lead=1,.tail=9};w.value=b;
    Wrapped z=wrap(w);if(z.lead!=3||z.tail!=6||!same(&z.value,&b))return 4;
    /* The by-value callee must not mutate the caller's source. */
    fill(&expect,3);if(!same(&a,&expect))return 5;
    if(parameter_alignment(a))return 6;
    return 0;
}
