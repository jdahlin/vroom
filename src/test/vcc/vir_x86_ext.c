#define NI __attribute__((noinline))
NI unsigned long long clean(unsigned x) {return x;}
NI unsigned long long trim(unsigned long long x) {return (unsigned)x;}
NI long long mask(const unsigned short *p,unsigned x) {return (long long)(*p&x);}
NI long long signedload(const signed char *p) {return *p;}
#if defined(__x86_64__)
extern unsigned dirtyret(void);
extern unsigned long long dirtyparam(void);
#ifdef _WIN64
#define ARG0 "%rcx"	/* (the Microsoft convention's first argument) */
#else
#define ARG0 "%rdi"
#endif
__asm__(".text\n.globl dirtyret\ndirtyret:\nmovabsq $0x6badbeefffffffff,%rax\nretq\n"
        ".globl dirtyparam\ndirtyparam:\nmovabsq $0x6badbeefffffffff," ARG0 "\njmp clean\n");
#else
unsigned dirtyret(void) {return ~0u;}
unsigned long long dirtyparam(void) {return clean(~0u);}
#endif
NI unsigned long long fromcall(void) {return dirtyret();}
int main(void) {
 if(dirtyparam()!=0xffffffffULL||fromcall()!=0xffffffffULL)return 1;
 if(trim(0x6badbeefffffffffULL)!=0xffffffffULL)return 2;
 unsigned short a[]={0,1,32767,32768,65535};
 unsigned x[]={0,1,0x80000000u,0xffffffffu,0x12345678u};
 for(unsigned i=0;i<5;i++)for(unsigned j=0;j<5;j++)
  if(mask(a+i,x[j])!=(long long)((unsigned)a[i]&x[j]))return 3;
 for(int i=-128;i<128;i++) {signed char c=i;if(signedload(&c)!=i)return 4;}
 return 0;
}
