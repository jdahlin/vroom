// [ext:gnu_asm_statements] asm statements (GCC manual 6.47): one function
// per line, the well-formed ones and the ones whose operands break a rule
// (an output is a modifiable lvalue whose constraint begins with = or +; an
// input's constraint begins with neither; a symbolic name is given once),
// each verdict compared with clang's.
int g, g2;
const int cg = 1;
int arr[4];
int fn(int);
struct S { int m; int &r; };
void ok1() { asm("nop"); }
void ok2() { asm volatile("" ::: "memory"); }
void ok3() { asm("" : "=r"(g) : "r"(g2)); }
void ok4() { asm("" : "+r"(g), "=&r"(g2) : "r"(cg), "i"(5) : "cc"); }
void ok5() { asm("" : [a] "=r"(g) : [b] "r"(g2)); }
void ok6() { asm("" : "=m"(arr[1]) : "r"(arr)); }
void ok7(S &s) { asm("" : "=r"(s.m), "=r"(s.r) : "r"(fn(g))); }
void ok8(int &r, int *p) { asm("" : "+r"(r) : "m"(*p)); }
void ok9() { asm("" : "=r"(g) : "r"(fn)); }
void ok10() { asm goto("" : : : : done); done:; }
void bad1() { asm("" : "r"(g)); }
void bad2() { asm("" : : "=r"(g)); }
void bad3() { asm("" : "=r"(cg)); }
void bad4() { asm("" : "=r"(1)); }
void bad5() { asm("" : [a] "=r"(g), [a] "=r"(g2)); }
void bad6() { asm("" : : "r"(undeclared_name)); }
void bad7() { asm("" : "=r"(fn)); }
void bad8() { asm("" : "+r"(g + 1)); }
