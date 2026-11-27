/* [class.inhctor.init]/1: an inherited constructor's class parameters that
   are not trivially copied and destroyed are the inherited constructor's
   own parameter objects: no copy or move is added, the caller destroys
   them after the base and members are initialized. */
extern "C" int printf(const char *, ...);

static int step;

struct Counted {
    int v;
    Counted(int x) : v(x) { printf("%d make %d\n", ++step, v); }
    Counted(const Counted &o) : v(o.v + 100)
    {
        printf("%d copy %d\n", ++step, v);
    }
    Counted(Counted &&o) : v(o.v + 1000) { printf("%d move %d\n", ++step, v); }
    ~Counted() { printf("%d drop %d\n", ++step, v); }
};

/* points into itself: a byte copy would show */
struct Self {
    Self *self;
    Self() : self(this) {}
    Self(const Self &) : self(this) {}
};

/* trivially copied, not trivially destroyed */
struct Dropped {
    int v;
    ~Dropped() { printf("%d dropped %d\n", ++step, v); }
};

/* neither copyable nor movable: only a prvalue argument reaches it */
struct Pinned {
    int v;
    Pinned(int x) : v(x) {}
    Pinned(const Pinned &) = delete;
    ~Pinned() { printf("%d unpin %d\n", ++step, v); }
};

struct Member {
    Member() { printf("%d member\n", ++step); }
    ~Member() { printf("%d ~member\n", ++step); }
};

struct Base {
    int sum;
    Base(Counted a) : sum(a.v) { printf("%d base1 %d\n", ++step, sum); }
    Base(Counted a, int n, Counted b) : sum(a.v * n + b.v)
    {
        printf("%d base3 %d\n", ++step, sum);
    }
    Base(Self s, int) : sum(s.self == &s) {}
    Base(Dropped d, char) : sum(d.v) {}
    Base(Pinned p, long) : sum(p.v) {}
};

struct Derived : Base {
    using Base::Base;
    Member m;
};

struct Again : Derived {
    using Derived::Derived;
};

int main()
{
    {
        Derived d(Counted(1));
        printf("%d sum %d\n", ++step, d.sum);
    }
    {
        Counted c(2);
        Derived d(c);
        printf("%d sum %d\n", ++step, d.sum);
        Derived e(static_cast<Counted &&>(c), 3, Counted(4));
        printf("%d sum %d\n", ++step, e.sum);
    }
    {
        Again a(Counted(5), 6, 7);
        printf("%d sum %d\n", ++step, a.sum);
    }
    {
        Derived s(Self(), 0);
        Again p(Pinned(8), 0L);
        Derived d(Dropped{9}, 'x');
        printf("%d sum %d %d %d\n", ++step, s.sum, p.sum, d.sum);
    }
    Base *h = new Derived(Counted(10));
    printf("%d sum %d\n", ++step, h->sum);
    delete static_cast<Derived *>(h);
    return 0;
}
