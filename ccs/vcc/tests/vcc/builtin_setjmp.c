extern void abort(void);
#if defined(__x86_64__)
static void *buf[5];
static volatile int steps, evals;
static void **once(void) { ++evals; return buf; }
__attribute__((noinline)) static void jump(int n) {
 volatile char space[113];
 space[0] = n;
 if (n) jump(n-1);
 if (space[0] != 0) abort();
 __builtin_longjmp(buf,1);
}
__attribute__((noinline)) static int value(void) { return 42; }
static void test(void) {
 volatile int local=123;
 int result=value()+__builtin_setjmp(once());
 if (result==42) { ++steps; jump(3); }
 if (result!=43 || local!=123 || steps!=1 || evals!=1) abort();
}
#endif
int main(void) {
#if defined(__x86_64__)
 test();
#endif
 return 0;
}
