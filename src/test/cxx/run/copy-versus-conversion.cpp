// [over.ics.rank]/2: a standard reference binding beats an unknown
// user-defined conversion between unrelated complete classes.
struct B {};
struct A {
    int n;
    A() : n(1) {}
    A(const A& a) : n(a.n + 1) {}
    A(B&&) : n(99) {}
    operator B() const { return B{}; }
};
int main() { const A a; A b(a); return b.n != 2; }
