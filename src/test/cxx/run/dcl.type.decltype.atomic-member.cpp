// [dcl.type.decltype]: decltype of an unparenthesized _Atomic member access
// keeps the declared atomic type.
struct A { _Atomic(int) v; };
template <class T, class U> constexpr bool same = false;
template <class T> constexpr bool same<T, T> = true;
int f(const A *a) {
    using T = decltype(a->v);
    return same<T, _Atomic(int)>;
}

int main() {
    A a{3};
    return f(&a) != 1;
}
