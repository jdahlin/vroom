/* the C front end's __attribute__((overloadable)) functions, which clang
   mangles as C++ (frontend/c/mangle.cpp on frontend/itanium/writer.h) */
#define O __attribute__((overloadable))
struct S {
    int x;
};
typedef struct {
    int y;
} T;
enum E { e };
O void f(void) {}
O void f(int i) { (void)i; }
O void f(char c, unsigned long u, long long l) { (void)c, (void)u, (void)l; }
O void f(float a, double b, long double c) { (void)a, (void)b, (void)c; }
O void f(const int *p, volatile char *q, int *restrict r) { (void)p, (void)q, (void)r; }
O void f(struct S *s, struct S *t) { (void)s, (void)t; }
O void f(T t, enum E x) { (void)t, (void)x; }
O void f(int (*g)(int, ...)) { (void)g; }
O void f(int (*a)[4]) { (void)a; }
O void f(_Bool b, __int128 n, unsigned short s) { (void)b, (void)n, (void)s; }
O static void g(int i) { (void)i; }
O void use(void) { g(1); }
// MANGLE: _Z1fv
// MANGLE: _Z1fi
// MANGLE: _Z1fcmx
// MANGLE: _Z1ffde
// MANGLE: _Z1fPKiPVcPi
// MANGLE: _Z1fP1SS0_
// MANGLE: _Z1f1T1E
// MANGLE: _Z1fPFiizE
// MANGLE: _Z1fPA4_i
// MANGLE: _Z1fbnt
// MANGLE: _ZL1gi
// MANGLE: _Z3usev
