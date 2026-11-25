// An empty base class subobject of an xvalue (std::move of a derived
// object: the copy of libc++'s __tree's comparator) and of a prvalue of a
// class with a constructor (its temporary, [conv.rval]).
extern "C" int printf(const char *, ...);
struct E { int f() const { return 4; } };
struct D : E { };
struct C : E { int v; C() : v(3) {} };
template <class T> T &&mv(T &t) { return static_cast<T &&>(t); }
D d;
int g(const E &e) { return e.f(); }
int main() {
    int a = g(mv(d));
    E c(mv(d));
    E c2 = C();
    int b = C().f();
    printf("%d %d %d %d\n", a, b, c.f(), c2.f());
    return a + b + c.f() + c2.f() - 16;
}
