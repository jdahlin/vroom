/* Stack-relative locals/spills, incoming/outgoing stack arguments, rbp and CFI. */
#define NI __attribute__((noinline))
#if defined(__x86_64__)
extern int backtrace(void **,int);
NI int frames(void) {void *p[32];return backtrace(p,32);}
#else
NI int frames(void) {return 32;}
#endif
NI unsigned long pressure(unsigned long a,unsigned long b,unsigned long c,unsigned long d,
                          unsigned long e,unsigned long f,unsigned long g,unsigned long h) {
 int n=frames();return n>=4?a+b+c+d+e+f+g+h:0;
}
NI unsigned long callee(void) {return pressure(1,2,3,4,5,6,7,8);}
#if defined(__x86_64__)
extern unsigned long checkbp(void);
__asm__(".text\n.globl checkbp\ncheckbp:\n.cfi_startproc\npushq %rbp\n"
        ".cfi_def_cfa_offset 16\n.cfi_offset %rbp,-16\n"
        "movabsq $0x6badbeefffffffff,%rbp\ncall callee\n"
        "movabsq $0x6badbeefffffffff,%r10\ncmpq %rbp,%r10\n"
        "setne %r10b\nmovzbl %r10b,%r10d\naddq %r10,%rax\npopq %rbp\n"
        ".cfi_def_cfa_offset 8\n.cfi_restore %rbp\nretq\n.cfi_endproc\n");
#else
unsigned long checkbp(void) {return callee();}
#endif
int main(void) {return checkbp()!=36;}
