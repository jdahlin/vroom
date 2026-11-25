// [temp.deduct.type]: a non-type template parameter whose type depends on an
// earlier one (a pointer to member), deduced and then used in the body.
template <int N> struct Acc { int a; };
struct S { Acc<4> x; };
template <int N, Acc<N> S::*P> struct F {};
template <int N, Acc<N> S::*P> int get(const F<N, P> &, S &s) {
    return (s.*P).a + N;
}
struct A {
    template <int C, int N, Acc<N> S::*P> int g(const F<N, P> &f, S &s) {
        return get(f, s) + C;
    }
    template <int N, Acc<N> S::*P> int h(const F<N, P> &f, S &s) {
        return g<1>(f, s);
    }
};

int main() {
    S s{{10}};
    F<4, &S::x> f;
    A a;
    return a.h(f, s) != 15;
}
