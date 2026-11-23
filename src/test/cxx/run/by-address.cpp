// [abi:calls]: a class that is non-trivial for the purposes of calls is
// passed by address: the caller constructs a temporary (by the copy
// constructor, or a prvalue's constructor in place), passes its address,
// and destroys it at the end of the full-expression ([class.temporary]/4)
extern "C" int printf(const char *, ...);

int events[64];
int count;
void note(int e) { events[count++] = e; }

struct T {
    int v;
    T(int x) : v(x) { note(1); }
    T(const T &o) : v(o.v + 100) { note(2); }
    ~T() { note(3); }
    int get() const { return v; }
};
struct CopyOnly {
    int a, b, c;
    CopyOnly(int x) : a(x), b(x * 2), c(x * 3) {}
    CopyOnly(const CopyOnly &o) : a(o.a), b(o.b), c(o.c + 1) {}
};
struct Wrapper {
    T t;
    int w;
    Wrapper(int x) : t(x), w(x) {}
};

int take(T t) {
    note(4);
    t.v += 1;
    return t.v;
}
int two(T a, T b) { return a.v * 10 + b.v; }
int copies(CopyOnly c) { return c.a + c.b + c.c; }
int wrapped(Wrapper w) { return w.t.v + w.w; }
struct S {
    int k;
    int method(T t) const { return k + t.get(); }
};

int main() {
    int r = 0;
    {
        T x(5);
        r += take(x);
        r += take(T(7));
        r += take(9);
        r += two(x, T(2));
        note(9);
        S s = {3};
        r += s.method(x);
        int y = take(x) + 1;
        r += y;
    }
    CopyOnly c(4);
    r += copies(c) + copies(CopyOnly(1));
    Wrapper w(6);
    r += wrapped(w);
    for (int i = 0; i < count; i++)
        r = (r * 3 + events[i]) % 100003;
    printf("%d %d\n", r, count);
    return 0;
}
