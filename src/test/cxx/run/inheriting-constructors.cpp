/* [namespace.udecl]/3, [class.inhctor.init]/1: a using-declarator that
   names the constructors of a direct base: the base is initialized by the
   inherited constructor, the other subobjects by default initialization
   (default member initializers run); the base's copy/move and default
   constructors are not inherited; the derived class's own hide the base's
   of the same signature. */
extern "C" int printf(const char *, ...);

struct Tag {
    int t = 9;
};

struct B {
    int v;
    B() : v(-1) {}
    B(int x) : v(x) {}
    B(int a, int b) : v(a * 10 + b) {}
    B(const char *s, long n) : v(n) { (void)s; }
    B(const B &) = default;
    explicit B(double d) : v((int)d + 100) {}
    B(const Tag &t, Tag &&u) : v(t.t + u.t) {}
    B(Tag *p, int &r) : v(*&r + p->t) { r = 42; }
};

struct D : B {
    using B::B;
    int w = 4;
    Tag tag;
};

struct E : B {
    using B::B;
    E(int x) : B(x + 1000) {}
    int w = 5;
};

struct F : D {
    using D::D;
    int z = 6;
};

/* a constructor of the derived class beside the using-declaration: the
   base's default constructor is inherited ([over.match.funcs]) and the
   derived class's own is preferred on a tie ([over.match.best]/2.6) */
struct X0 {
    int q;
    X0() : q(5) {}
    X0(long) : q(6) {}
    X0(int, int) : q(7) {}
};
struct Del {
    int d;
    Del(int x) : d(x) {}
    Del(long) = delete;
};
struct UsesDel : Del {
    using Del::Del;
};
struct Y0 : X0 {
    using X0::X0;
    Y0(int, int, int = 0) : X0(100) {}
};

int main()
{
    UsesDel ud(4);
    Y0 y0;
    Y0 y1(2L);
    Y0 y2(1, 2);
    printf("%d %d %d %d\n", y0.q, y1.q, y2.q, ud.d);
    D a(3);
    D b(1, 2);
    D c;
    D d(a);
    D e("x", 77L);
    D f(2.5);
    D g{7};
    Tag t1, t2;
    D h(t1, static_cast<Tag &&>(t2));
    int r = 1;
    Tag t;
    D i(&t, r);
    E x(1);
    E y(1, 2);
    F ff(8);
    F fg(3, 4);
    printf("%d %d %d %d | %d %d %d | %d %d %d | %d %d %d\n", a.v, a.w, a.tag.t,
           b.v, c.v, d.v, e.v, f.v, g.v, h.v, i.v, r, x.v);
    printf("%d %d %d %d %d %d\n", y.v, x.w, ff.v, ff.w, ff.z, fg.v);
    return 0;
}
