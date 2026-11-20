// A class whose implicit copy constructor is deleted is constructed from a
// non-class argument by its own constructor ([over.match.ctor]/1: the
// deleted copy constructor is no candidate for it, [over.best.ics]/4), as
// libc++'s latch holding an atomic; a class with a member that declares a
// move constructor and its own defaulted copy constructor gets no implicit
// move constructor ([class.copy.ctor]/8), as nested_exception.
extern "C" int printf(const char *, ...);

struct Counter {
    int value;
    Counter(int v) : value(v) {}
    Counter(const Counter &) = delete;
    Counter &operator=(const Counter &) = delete;
};

struct Latch {
    Counter count;
    explicit Latch(int n) : count(n) {}
};

struct Ptr {
    int *p;
    Ptr() : p(0) {}
    Ptr(const Ptr &o) : p(o.p) {}
    Ptr(Ptr &&o) : p(o.p) { o.p = 0; }
    ~Ptr() {}
};

struct Nested {
    Ptr ptr;
    Nested() {}
    Nested(const Nested &) = default;
    ~Nested() {}
};

int main()
{
    Latch l(7);
    int x = 3;
    Nested a;
    a.ptr.p = &x;
    Nested b(a);
    printf("%d %d\n", l.count.value, *b.ptr.p);
    return 0;
}
