// std: c++20
// C++20's rewritten candidates: a != b with only operator== declared is !(a == b), member or friend
struct It { int *p; bool operator==(const It &o) const { return p == o.p; } };
template <class T> struct W { T *p; friend bool operator==(const W &a, const W &b) { return a.p == b.p; } };
int main() { int x[2]; It a{x}, b{x + 1}; W<int> c{x}, d{x}; return (a != b) && !(c != d) ? 0 : 1; }
