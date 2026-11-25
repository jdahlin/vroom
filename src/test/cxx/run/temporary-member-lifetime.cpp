// [class.temporary]/4: a temporary used as a member call's object lives to
// the end of the full-expression, including when the member returns a reference.
static int destroyed;
struct A {
    int n;
    A(int v) : n(v) {}
    A(const A& a) : n(a.n) {}
    ~A() { ++destroyed; }
    A& member() { n += 2; return *this; }
    template<class T> A& add(T v) { n += v; return *this; }
};
A make() { return A(3); }
int main() {
    A a = make().member();
    if (a.n != 5 || destroyed != 1) return 1;
    A b = make().add(4);
    return b.n != 7 || destroyed != 2;
}
