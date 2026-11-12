#define NI __attribute__((noinline))
NI unsigned long clean(unsigned x) {return x;}
NI unsigned long trim(unsigned long x) {return (unsigned)x;}
NI long mask(const unsigned short *p,unsigned x) {return (long)(*p&x);}
NI long signedload(const signed char *p) {return *p;}
#if defined(__x86_64__)
extern unsigned dirtyret(void);
extern unsigned long dirtyparam(void);
__asm__(".text\n.globl dirtyret\ndirtyret:\nmovabsq $0x6badbeefffffffff,%rax\nretq\n"
        ".globl dirtyparam\ndirtyparam:\nmovabsq $0x6badbeefffffffff,%rdi\njmp clean\n");
#else
unsigned dirtyret(void) {return ~0u;}
unsigned long dirtyparam(void) {return clean(~0u);}
#endif
NI unsigned long fromcall(void) {return dirtyret();}
int main(void) {
 if(dirtyparam()!=0xffffffffUL||fromcall()!=0xffffffffUL)return 1;
 if(trim(0x6badbeefffffffffUL)!=0xffffffffUL)return 2;
 unsigned short a[]={0,1,32767,32768,65535};
 unsigned x[]={0,1,0x80000000u,0xffffffffu,0x12345678u};
 for(unsigned i=0;i<5;i++)for(unsigned j=0;j<5;j++)
  if(mask(a+i,x[j])!=(long)((unsigned)a[i]&x[j]))return 3;
 for(int i=-128;i<128;i++) {signed char c=i;if(signedload(&c)!=i)return 4;}
 return 0;
}
