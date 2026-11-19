// [namespace.udecl]/1, /13: a function template declared in a scope where a
// using-declaration introduced templates of the same name joins their
// overload set (libc++'s std::get after using ranges::get)
namespace m {
template <int I, class T> int get(T *) { return I + 1; }
}
namespace n {
using m::get;
template <class... T> struct variant { int v; };
template <int I, class... T> int get(variant<T...> &x) { return x.v + I; }
template <int I, class... T> int get(variant<T...> &&x) { return x.v - I; }
}
int main() {
    n::variant<int, char> v{40};
    if (n::get<2>(v) != 42) return 1;
    return 0;
}
