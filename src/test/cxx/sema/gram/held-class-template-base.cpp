// A refused partial specialization declares its template name, not the
// template arguments or base-class names it uses.
using size_t = unsigned long;
template<int A, int B> struct H {};
template<class A, class B> struct Base {};
template<int N> struct H<N + 1, N> : Base<size_t, int> {};
size_t next(size_t n) { return n + 1; }
