/* [class.temporary]/6: a temporary bound to a reference member in the
   braced initializer of a static variable lives as long as it */
extern "C" int printf(const char *, ...);
struct A { int a; int &&r; };
int f() { return 41; }
A a1{1, f()};
const int &g = f();
int main() { printf("%d %d\n", a1.r + a1.a, g); return a1.r + a1.a - 42; }
