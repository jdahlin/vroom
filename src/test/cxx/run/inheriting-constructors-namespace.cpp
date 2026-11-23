/* [namespace.udecl]/3: the inheriting constructor of a base in a
   namespace ([abi:mangling-special-ctor-dtor]: CI2 and the base's name), a
   protected constructor of the base, an explicit one, and a base with a
   member of class type. */
extern "C" int printf(const char *, ...);

namespace ns {
struct M {
    int m;
    M() : m(3) {}
};
struct Base {
    int v;
    M mem;
    Base(int x, M y) : v(x + y.m), mem(y) {}

  protected:
    Base(long x) : v((int)x * 2) {}

  public:
    explicit Base(char c) : v(c) {}
};
} // namespace ns

struct Derived : ns::Base {
    using ns::Base::Base;
    int tail = 11;
};

struct Child : Derived {
    Child() : Derived(5L) {}
};

/* a base that is a class template's specialization */
template <class T> struct Box {
    T v;
    Box(T x) : v(x) {}
};
template <class T> struct Wrapped : Box<T> {
    using Box<T>::Box;
    int r = 1;
};

int main()
{
    Wrapped<int> wi(5);
    Wrapped<long> wl(6L);
    printf("%d %ld %d\n", wi.v, wl.v, wi.r + wl.r);
    ns::M m;
    Derived a(1, m);
    Derived b('A');
    Child c;
    printf("%d %d %d %d %d\n", a.v, a.tail, b.v, c.v, c.tail);
    return 0;
}
