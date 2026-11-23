// [abi:calls]: classes non-trivial for the purposes of calls returned
// through the result address (x8) whatever their size: prvalue returns,
// a named return value (clang's NRVO, [class.copy.elision]/1.1), results
// discarded or used as temporaries destroyed at the end of the
// full-expression, and passed on as arguments
extern "C" int printf(const char *, ...);
extern int events[64]; extern int count;
inline void note(int e) { events[count++] = e; }
struct T { int v; T(int x); T(const T &o); ~T(); };
struct C3 { long a, b, c; C3(long x); C3(const C3 &o); };
int take(T t); int takes(C3 c, T t, int k); int run_side();
T make_t(int x); T named_t(int x); C3 make_c3(long x); T pass_through(T t);

T::T(int x) : v(x) { note(1); }
T::T(const T &o) : v(o.v + 100) { note(2); }
T::~T() { note(3); }
C3::C3(long x) : a(x), b(x + 1), c(x + 2) {}
C3::C3(const C3 &o) : a(o.a), b(o.b), c(o.c * 2) {}
int take(T t) { note(4); return t.v++; }
int takes(C3 c, T t, int k) { return (int)(c.a + c.b + c.c) + t.v * k; }
T make_t(int x) { return T(x * 2); }
T named_t(int x) { T r(x); r.v += 1; return r; }
C3 make_c3(long x) { C3 c(x); return c; }
T pass_through(T t) { return t; }

int events[64]; int count;
int main() {
    int r = 0;
    { T x(5); r += take(x) + take(T(3)); C3 c(2); r += takes(c, x, 3); }
    { T a = make_t(4); T b = named_t(6); r += a.v * 7 + b.v; }
    { C3 c = make_c3(3); r += (int)(c.a + c.b + c.c); }
    { make_t(1); r += make_t(2).v + named_t(8).v; }
    { T x(1); T y = pass_through(x); r += y.v; r += take(make_t(9)); }
    for (int i = 0; i < count; i++) r = (r * 3 + events[i]) % 100003;
    printf("%d %d\n", r, count);
    return 0;
}
