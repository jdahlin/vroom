// [dcl.spec.auto]: naming a specialization of a member template with a
// placeholder return type deduces it.
template <class T>
struct S {
    template <class U>
    static constexpr decltype(auto) f(U x) { return x; }
};
auto p = S<int>::template f<int>;

int main() { return p(3) != 3; }
