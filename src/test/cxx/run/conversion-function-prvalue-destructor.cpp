// A conversion function called on a prvalue of a class with a destructor
// (nlohmann::json's output_adapter<char>(o) converted to its shared_ptr):
// the temporary is destroyed at the end of the full-expression
// ([class.temporary]/4), in a block and at namespace scope.
extern "C" int printf(const char *, ...);
int live = 0;
struct P { int v; };
struct A {
    int v;
    A(int x) : v(x) { live++; printf("A(%d)\n", v); }
    A(const A &o) : v(o.v) { live++; }
    ~A() { live--; printf("~A(%d) live=%d\n", v, live); }
    operator P() const { printf("conv %d\n", v); return P{v + 1}; }
    operator int() const { return v * 2; }
};
struct S { int x; S(P p, int y) : x(p.v + y) { printf("S live=%d\n", live); } };
int f(P p) { return p.v; }
int g = f(A(9));
int main() {
    S s(A(4), 1);
    int a = f(A(7));
    int b = A(3);
    printf("%d %d %d %d\n", s.x, a, b, live);
    return s.x + a + b + g - 30;
}
