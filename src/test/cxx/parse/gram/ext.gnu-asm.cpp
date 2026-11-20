// RUN: %clang_cc1 -std=gnu++17 %s
// [ext.gnu] asm statements (GCC manual 6.47, ext/gnu/asm-statement.cpp):
// basic and extended, with qualifiers, operands, clobbers
int in1, in2, out1, out2;
int g_array[4];
void a1() { asm("nop"); }
void a2() { __asm__("nop"); }
void a3() { __asm("nop"); }
void a4() { asm volatile("nop"); }
void a5() { asm __volatile__("nop"); }
void a6() { asm inline("nop"); }
void a7() { asm volatile inline("nop"); }
void a8() { asm("nop" "nop\n" "nop"); }
void a9() { asm("" : "=r"(out1)); }
void a10() { asm("" : "=r"(out1) : "r"(in1)); }
void a11() { asm("" : "=r"(out1), "=r"(out2) : "r"(in1), "r"(in2)); }
void a12() { asm("" : : "r"(in1)); }
void a13() { asm("" :: "r"(in1)); }
void a14() { asm("" : "=r"(out1) :: "memory"); }
void a15() { asm("" ::: "memory", "cc"); }
void a16() { asm("" :::); }
void a17() { asm("" : "+r"(out1) : "r"(in1) : "memory"); }
void a18() { asm("" : [o] "=r"(out1) : [i] "r"(in1)); }
void a19() { asm("%[i]" : : [i] "r"(in1 + 1)); }
void a20() { asm("" : "=m"(g_array[in1 & 3])); }
void a21() { asm("" : : "i"(sizeof(int))); }
void a22() { asm("" : "=r"(out1) : "r"((long)in1 > 3 ? in1 : in2)); }
void a23() { asm volatile("" : : : "memory"); }
void a24() { for (int i = 0; i < 2; i++) asm("nop"); }
void a25(bool b) { if (b) asm("nop"); else asm("nop"); }
void a26() { asm("" : "=r"(out1) : "r"(in1 > in2)); }
template <class T> void a27(T t) { asm("" : "=r"(out1) : "r"(t)); }
struct S { int m; void f() { asm("" : "+r"(m)); } };
void a28() { auto l = [] { asm("nop"); }; l(); }
void a29() { asm goto("" : : : : done); done:; }
