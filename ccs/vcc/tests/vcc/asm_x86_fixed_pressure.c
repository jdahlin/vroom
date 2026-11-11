/* Fixed read/write operands must not keep all their input values in
   temporary registers while the remaining operands are evaluated. */
#if defined(__x86_64__)
__attribute__((noinline)) static int run(int branch)
{
    int a = 1, b = 2, c = 3, d = 4, s = 5;
    __asm__ goto("addl $10, %0; addl $20, %1; addl $30, %2; "
                 "addl $40, %3; addl $50, %4; testl %5, %5; jnz %l[taken]"
                 : "+a"(a), "+b"(b), "+c"(c), "+d"(d), "+S"(s)
                 : "r"(branch) : "cc" : taken);
    return a + b + c + d + s;
taken:
    return a + b + c + d + s + 1;
}
int main(void) { return run(0) != 165 || run(1) != 166; }
#else
int main(void) { return 0; }
#endif
