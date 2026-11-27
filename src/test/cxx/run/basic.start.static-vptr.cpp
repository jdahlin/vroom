/* [basic.start.static]/2, [class.cdtor]: objects of dynamic classes of
   static storage duration (and the other shapes of default
   initialization) have their virtual table pointers, statically where the
   initialization is constant.  Returns the number of the failed check. */
struct P {
    virtual int f() { return 7; }
};

/* [basic.start.dynamic]: runs before the objects below are reached, which
   are constant-initialized */
extern P early;
static int early_value = early.f();
P early;

P g;
P ga[3];
P gm[2][2];
constexpr P cp;
constexpr P cpa[2];
constinit P ci;
static thread_local P tl;
static thread_local P tla[2];
inline P iv;
struct H {
    int a;
    P p;
    P q[2][2];
};
H h;
struct Ag {
    int a;
    P p;
    P r[2];
};
struct A {
    virtual int a() { return 1; }
    int x = 3;
};
struct B {
    virtual int b() { return 2; }
    long y = 4;
};
struct C : A, B {
    int a() override { return 11; }
    int b() override { return 12; }
};
C c;
C ca[2];
struct N {
    int n = 0;
};
struct M : N, B {
};
M m;
struct V : virtual A {
    int w = 0;
};
V v;
V va[2];
struct Q {
    int k;
    constexpr Q() : k(5) {}
    virtual int g() { return k; }
};
Q q;
constinit Q cq[2];
struct R : Q {
    long z = 1;
};
R rr;
template <class T> struct S {
    static P out;
    static inline P in;
    static inline C two[2];
};
template <class T> P S<T>::out;
template <class T> P vt;
struct X {
    static inline P in;
    static inline P two[2];
    static P out;
};
P X::out;

int made, gone;
struct U {
    int v;
    U() : v(3) { made++; }
    ~U() { gone++; }
};
struct Y {
    static inline U one;
    static inline U two[2];
};
struct W {
    U u[2][3];
};

template <class T> __attribute__((noinline)) T *o(T *p)
{
    asm volatile("" : "+r"(p));
    return p;
}

int main()
{
    static P s;
    static P sa[2];
    static H sh;
    static C sc;
    static V sv;
    static Q sq;
    thread_local P tls[2];
    thread_local C tc;
    P local[2];
    H lh;
    P gb{};
    P gp = P();
    P gl[2]{};
    Ag ag{};
    Ag ag2{1};
    int n = 0;
#define T(e, w)                                                               \
    n++;                                                                      \
    if ((e) != (w))                                                           \
        return n;
    T(early_value, 7)
    T(o(&g)->f(), 7)
    T(o(&ga[2])->f(), 7)
    T(o(&gm[1][1])->f(), 7)
    T(o(&gb)->f(), 7)
    T(o(&gp)->f(), 7)
    T(o(&gl[1])->f(), 7)
    T(o(const_cast<P *>(&cp))->f(), 7)
    T(o(const_cast<P *>(&cpa[1]))->f(), 7)
    T(o(&ci)->f(), 7)
    T(o(&tl)->f(), 7)
    T(o(&tla[1])->f(), 7)
    T(o(&iv)->f(), 7)
    T(o(&h.p)->f(), 7)
    T(o(&h.q[1][1])->f(), 7)
    T(o(&ag.r[1])->f(), 7)
    T(o(&ag2.r[1])->f(), 7)
    T(o<A>(&c)->a(), 11)
    T(o<B>(&c)->b(), 12)
    T(o<B>(&ca[1])->b(), 12)
    T(o<B>(&m)->b(), 2)
    T(c.x + c.y + m.y, 11)
    T(o<A>(&v)->a(), 1)
    T(o<A>(&va[1])->a(), 1)
    T(o(&q)->g(), 5)
    T(o(&cq[1])->g(), 5)
    T(o(&rr)->g(), 5)
    T(o(&S<int>::out)->f(), 7)
    T(o(&S<int>::in)->f(), 7)
    T(o<B>(&S<int>::two[1])->b(), 12)
    T(o(&vt<int>)->f(), 7)
    T(o(&X::in)->f(), 7)
    T(o(&X::two[1])->f(), 7)
    T(o(&X::out)->f(), 7)
    T(Y::one.v + Y::two[1].v, 6)
    T(o(&s)->f(), 7)
    T(o(&sa[1])->f(), 7)
    T(o(&sh.q[1][0])->f(), 7)
    T(o<B>(&sc)->b(), 12)
    T(o<A>(&sv)->a(), 1)
    T(o(&sq)->g(), 5)
    T(o(&tls[1])->f(), 7)
    T(o<B>(&tc)->b(), 12)
    T(o(&local[1])->f(), 7)
    T(o(&lh.q[1][1])->f(), 7)
    int before = made, destroyed = gone;
    {
        W w;
        T(w.u[1][2].v, 3)
    }
    T(made - before, 6)
    T(gone - destroyed, 6)
    return 0;
}
