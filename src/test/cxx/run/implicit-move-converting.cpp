/* [class.copy.elision]/3: a returned local converted to another class is
   first taken as an rvalue: which constructor or conversion function is
   chosen, and how often each object is destroyed */
extern "C" int printf(const char *, ...);

static int chosen, destroyed;

struct Source {
    int v;
    Source(int v) : v(v) {}
    Source(const Source &o) : v(o.v) {}
    Source(Source &&o) : v(o.v) { o.v = 0; }
    ~Source() { destroyed += 1; }
};

struct Optional {
    int how, v;
    Optional(const Source &s) : how(1), v(s.v) {}
    Optional(Source &&s) : how(2), v(s.v) {}
    ~Optional() { destroyed += 100; }
};

struct ByValue {
    Source s;
    ByValue(Source s) : s(static_cast<Source &&>(s)) {}
};

struct ConstOnly {
    int how;
    ConstOnly(const Source &) : how(3) {}
    ConstOnly(Source &) : how(4) {}
};

/* the rvalue resolution fails: the second takes the lvalue */
struct MutableOnly {
    int how;
    MutableOnly(Source &) : how(5) {}
};

template <class T> struct Ref {
    T *p;
    Ref(T *p) : p(p) {}
    Ref(const Ref &o) : p(o.p) { chosen = 6; }
    template <class U> Ref(const Ref<U> &o) : p(o.p) { chosen = 7; }
    template <class U> Ref(Ref<U> &&o) : p(o.p)
    {
        o.p = nullptr;
        chosen = 8;
    }
    ~Ref() { destroyed += p != nullptr; }
};

struct Base {
    int how;
    Base() : how(0) {}
    Base(const Base &) : how(9) {}
    Base(Base &&) : how(10) {}
};
struct Derived : Base {};

struct Target {
    int how;
    Target(int how) : how(how) {}
};
struct Converts {
    operator Target() { return Target(11); }
};

static Optional optional()
{
    Source s(7);
    return s;
}
static Optional parameter(Source s) { return (s); }
static ByValue by_value()
{
    Source s(8);
    return s;
}
static ConstOnly const_only()
{
    Source s(1);
    return s;
}
static MutableOnly mutable_only()
{
    Source s(1);
    return s;
}
struct Wide {
    int x;
};
struct Narrow : Wide {};
static Ref<Wide> ref()
{
    static Narrow narrow;
    Ref<Narrow> r(&narrow);
    return r;
}
static Base base()
{
    Derived d;
    return d;
}
static Target target()
{
    Converts c;
    return c;
}

static void report(const char *what, int a, int b)
{
    printf("%s %d %d destroyed %d\n", what, a, b, destroyed);
    destroyed = 0;
}

int main()
{
    {
        Optional o = optional();
        report("optional", o.how, o.v);
    }
    report("end", 0, 0);
    {
        Optional o = parameter(Source(5));
        report("parameter", o.how, o.v);
    }
    report("end", 0, 0);
    {
        ByValue b = by_value();
        report("by value", b.s.v, 0);
    }
    report("end", 0, 0);
    report("const", const_only().how, 0);
    report("mutable", mutable_only().how, 0);
    {
        Ref<Wide> r = ref();
        report("ref", chosen, r.p != nullptr);
    }
    report("end", 0, 0);
    report("base", base().how, 0);
    report("conversion", target().how, 0);
    return 0;
}
