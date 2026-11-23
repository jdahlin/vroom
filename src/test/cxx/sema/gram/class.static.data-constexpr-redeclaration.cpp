// [class.static.data]/3 (C++11, C++14) and [depr.static.constexpr]/1 (C++17):
// a constexpr static data member initialized in its class, declared again at
// namespace scope without an initializer
struct A { static constexpr int x = 1; };
constexpr int A::x;
int f() { return A::x; }
