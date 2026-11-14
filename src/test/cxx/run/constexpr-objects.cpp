// namespace constexpr constants: integers by value, a floating one an object of its own (_ZN1nL1eE)
namespace n { constexpr int a = -60; constexpr int b = a + 3; const double d = 2.5; constexpr double e = 1.5; int f() { return a + b + (int)d + (int)(e * 2); } }
int main() { return n::f() == -112 ? 0 : 1; }
