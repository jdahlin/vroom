/* [namespace.udecl]: a protected static member made public by a
   using-declaration, named through the derived class */
extern "C" int printf(const char *, ...);
class B {
protected:
    static void f(int x) { printf("f int %d\n", x); }
    static void f(int x, int y) { printf("f2 %d %d\n", x, y); }
    static void g(int x) { printf("g %d\n", x); }
    static void h(int x) { printf("h %d\n", x); }
    static void h(char x) { printf("h char %d\n", x); }
};
namespace { class D : public B { public: using B::f; using B::g; using B::h; }; }
struct E { int n; };
int main() { D::f(1); D::f(2, 3); D::g(4); D::h(5); D::h('a'); auto r = [&](int k) { D::f(k); }; r(6); }
