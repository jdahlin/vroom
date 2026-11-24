extern "C" int puts(const char *);
extern "C" void abort();
struct D { int *p; D(int *q) : p(q) {} ~D() { ++*p; } };
struct E { int v; E(int x) : v(x) {} E(const E &o) : v(o.v) {} };
static int hits;
int thrower(int k) {
    D d(&hits);
    if (k == 1) throw 7;
    if (k == 2) throw E(8);
    if (k == 3) throw (long)9;
    return 0;
}
int main() {
    int r = 0;
    try { r += thrower(0); } catch (...) { return 1; }
    try { thrower(1); return 2; } catch (int i) { r += i; }
    try { thrower(2); return 3; } catch (const E &e) { r += e.v; }
    try { thrower(3); return 4; } catch (int) { return 5; } catch (...) { r += 100; }
    return r == 115 && hits == 4 ? 0 : 10 + r;
}
