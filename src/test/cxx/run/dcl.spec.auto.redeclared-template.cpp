// [dcl.spec.auto]: a function template with a placeholder return type declared
// before its definition.
template <class T, class... Vs, class = int>
constexpr decltype(auto) foo(T x, Vs...);
template <class T, class... Vs, class>
constexpr decltype(auto) foo(T x, Vs...) { return x; }

int main() { return foo(1, 2) != 1; }
