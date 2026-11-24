// Throw and catch ([except]): by value, by reference, by pointer, a base class, catch (...), rethrow, destructors
// run while unwinding, nested try-blocks, a handler that throws.  No library headers.
static int order[32], n_order;
static void note(int k) { order[n_order++] = k; }
static bool order_is(const int *want, int n)
{
    bool ok = n_order == n;
    for (int i = 0; i < n && ok; i++)
        ok = order[i] == want[i];
    n_order = 0;
    return ok;
}

struct Guard {
    int id;
    Guard(int i) : id(i) {}
    ~Guard() { note(id); }
};
struct Base {
    int b;
    Base(int x) : b(x) {}
    virtual ~Base() {}
};
struct Derived : Base {
    int d;
    Derived(int x, int y) : Base(x), d(y) {}
};
struct Copy {
    int v;
    static int copies;
    Copy(int x) : v(x) {}
    Copy(const Copy &o) : v(o.v) { copies++; }
};
int Copy::copies;

static void throw_int(int v) { Guard g(1); throw v; }
static void throw_derived() { Guard g(2); throw Derived(10, 20); }
static void throw_copy() { Copy c(5); throw c; }
static void throw_pointer(int *p) { throw p; }
static void throw_text() { Guard g(3); throw "text"; }
static void nested_guards(int k)
{
    Guard a(10);
    {
        Guard b(11);
        if (k)
            throw 77;
    }
    Guard c(12);
}
static void rethrower()
{
    try {
        throw_int(4);
    } catch (int i) {
        note(100 + i);
        throw;
    }
}

int main()
{
    int r = 0;
    try { throw_int(3); return 1; } catch (int i) { r = i; }
    { static const int w[] = {1}; if (r != 3 || !order_is(w, 1)) return 2; }

    try { throw_derived(); return 3; } catch (const Base &b) { r = b.b; }
    { static const int w[] = {2}; if (r != 10 || !order_is(w, 1)) return 4; }

    try { throw_derived(); return 5; } catch (Derived d) { r = d.d; }
    { static const int w[] = {2}; if (r != 20 || !order_is(w, 1)) return 6; }

    Copy::copies = 0;
    try { throw_copy(); return 7; } catch (Copy c) { r = c.v; }
    if (r != 5 || Copy::copies != 2) return 8; // throw copies once, by-value handler once

    int target = 9;
    try { throw_pointer(&target); return 9; } catch (int *p) { r = *p; }
    if (r != 9) return 10;

    try { throw_text(); return 11; } catch (const char *s) { r = s[0]; }
    { static const int w[] = {3}; if (r != 't' || !order_is(w, 1)) return 12; }

    try { throw_int(8); return 13; } catch (long) { return 14; } catch (...) { r = 1; }
    { static const int w[] = {1}; if (r != 1 || !order_is(w, 1)) return 15; }

    try { nested_guards(1); return 16; } catch (int i) { r = i; }
    { static const int w[] = {11, 10}; if (r != 77 || !order_is(w, 2)) return 17; }
    nested_guards(0);
    { static const int w[] = {11, 12, 10}; if (!order_is(w, 3)) return 18; }

    try { rethrower(); return 19; } catch (int i) { r = i; }
    { static const int w[] = {1, 104}; if (r != 4 || !order_is(w, 2)) return 20; }

    try {
        try { throw_int(6); return 21; } catch (const char *) { return 22; }
    } catch (int i) { r = i; }
    { static const int w[] = {1}; if (r != 6 || !order_is(w, 1)) return 23; }

    try {
        try { throw_int(7); return 24; } catch (int i) { r = i; throw Guard(30); }
    } catch (const Guard &g) { r += g.id; }
    { static const int w[] = {1, 30}; if (r != 37 || !order_is(w, 2)) return 25; }

    for (int i = 0; i < 3; i++) {
        try {
            if (i == 1) throw i;
            r += 1;
        } catch (int k) {
            r += 10 * k;
            continue;
        }
        r += 100;
    }
    if (r != 37 + 1 + 100 + 10 + 1 + 100) return 26;
    return 0;
}
