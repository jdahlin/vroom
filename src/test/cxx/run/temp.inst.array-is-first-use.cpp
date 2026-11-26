// [temp.inst]/2: an array that is the first use of a class template
// specialization instantiates it, and its elements are constructed.
extern "C" int printf(const char *, ...);
int made;
struct M { int v; M() : v(7) { made++; } };
template <class A, class B> struct P { A a; B b; P() : a(), b() {} };
struct Q { M first; M second; };
__attribute__((noinline)) void dirty() { volatile unsigned char b[4096]; for (int i = 0; i < 4096; i++) b[i] = 0xab; }
__attribute__((noinline)) int run() {
    P<M, int> p[2]; Q q[3]; P<int, int> z[2];
    printf("made %d p %d %d q %d %d z %d %d\n", made, p[0].a.v, p[1].a.v, q[2].first.v, q[2].second.v, z[1].a, z[1].b);
    return made != 8;
}
int main() { dirty(); return run(); }
