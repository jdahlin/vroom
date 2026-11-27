/* [ext.clang] #pragma pack(n) with bit-fields: each at the next bit, the
   class's alignment capped at n. */
extern "C" int printf(const char *, ...);
extern "C" void *memset(void *, int, unsigned long);
template <class T> void dump(const T &t)
{
    const unsigned char *p = reinterpret_cast<const unsigned char *>(&t);
    printf("%d %d:", static_cast<int>(sizeof(T)),
           static_cast<int>(alignof(T)));
    for (unsigned i = 0; i < sizeof(T); i++)
        printf(" %02x", p[i]);
    printf("\n");
}
#pragma pack(push, 1)
struct A { unsigned char a : 1, b : 3, c : 7, d : 5; };
struct B { char x; int a : 20; int b : 20; short c : 3; char y; };
union U { int a : 12; char b; };
struct N { char x; long long : 11; char y; };
#pragma pack(pop)
#pragma pack(push, 2)
struct C { char x; int a : 20; int b : 20; long long c : 40; char y; };
struct D { char x; short a : 9; short b : 9; };
union V { long long a : 33; char b; };
#pragma pack(pop)
#pragma pack(push, 8)
struct E { char x; int a : 30; int b : 4; char y; };
#pragma pack(pop)
struct Wide { int w; char x; };
struct Empty {};
struct Narrow { short s; };
#pragma pack(push, 1)
struct F : Wide { char y; int z; };
struct G : Narrow, Empty, Wide { char y; };
union H { struct H1 : Wide { char q; } h; char k; };
#pragma pack(pop)
#pragma pack(push, 2)
struct I : Narrow, Wide { char y; long long z; };
struct J : Empty { int z; };
#pragma pack(pop)
template <class T> T zeroed()
{
    T t;
    memset(&t, 0, sizeof t);
    return t;
}
int main()
{
    A a = zeroed<A>(); a.a = 1; a.b = 5; a.c = 99; a.d = 17; dump(a);
    B b = zeroed<B>(); b.x = 1; b.a = 0x12345; b.b = -2; b.c = 2; b.y = 9; dump(b);
    U u = zeroed<U>(); u.a = 0x5a5; dump(u);
    N n = zeroed<N>(); n.x = 1; n.y = 2; dump(n);
    C c = zeroed<C>(); c.x = 1; c.a = 0x54321; c.b = 7; c.c = 0x123456789LL; c.y = 3; dump(c);
    D d = zeroed<D>(); d.x = 1; d.a = 200; d.b = -100; dump(d);
    V v = zeroed<V>(); v.a = 0xabcdef12LL; dump(v);
    E e = zeroed<E>(); e.x = 1; e.a = 123456; e.b = 5; e.y = 4; dump(e);
    F f = zeroed<F>(); f.w = 1; f.x = 2; f.y = 3; f.z = 4; dump(f);
    G g = zeroed<G>(); g.s = 5; g.w = 1; g.x = 2; g.y = 3; dump(g);
    H h = zeroed<H>(); h.h.w = 6; h.h.q = 7; dump(h);
    I i = zeroed<I>(); i.s = 5; i.w = 1; i.x = 2; i.y = 3; i.z = 4; dump(i);
    J j = zeroed<J>(); j.z = 8; dump(j);
    printf("%d %d %d %d\n", b.a, b.b, static_cast<int>(c.c % 1000), d.b);
}
