/* -Wreturn-type and -Winvalid-noreturn: reachability as clang's CFG (constant conditions, noreturn calls, goto) */
_Noreturn void a(void) { }
__attribute__((noreturn)) void b(int x) { if (x) return; }
_Noreturn void c(void) { for (;;) ; }
void abort(void) __attribute__((noreturn));
_Noreturn void d(int x) { if (x) abort(); }
_Noreturn void e(void) { abort(); }
_Noreturn int f(void) { while (1) {} }
int g1(int x) { if (x) return 1; }
int g2(void) { }
int g3(int x) { switch (x) { case 1: return 1; default: return 2; } }
int g4(int x) { while (1) { if (x) break; } }
int g5(int x) { for (;;) { if (x) return 1; } }
int g6(int x) { if (x) abort(); else return 1; }
int g7(int x) { if (x) goto l; return 0; l: ; }
int g8(int x) { do { return 1; } while (0); }
int g9(int x) { switch (2) { case 2: return 1; } }
int g10(int x) { switch (3) { case 2: return 1; } }
int g11(int x) { x ? abort() : (void)0; }
int g12(void) { __builtin_unreachable(); }
int g13(int x) { if (0) return 1; }
int g14(int x) { switch (x) { case 1: return 1; } }
int g15(int x) { while (x) return 1; }
int main(void) { }
