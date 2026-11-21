// [class.derived]/2: a qualified template-id can name a base class.
namespace detail {
template<class T, int N> struct Storage {
    T value;
    Storage(T n) : value(n + N) {}
};
template<class T> using Alias = Storage<T, 3>;
}
template<class T> struct Derived : detail::Storage<T, 2> {
    Derived(T n) : detail::Storage<T, 2>(n) {}
};
struct Aliased : detail::Alias<int> {
    Aliased() : detail::Alias<int>(7) {}
};
struct Outer {
    template<class T> struct Nested { T value; };
};
struct NestedDerived : Outer::Nested<int> {};
int main() {
    Derived<int> a(5);
    Aliased b;
    NestedDerived c;
    c.value = 11;
    return a.value != 7 || b.value != 10 || c.value != 11;
}
