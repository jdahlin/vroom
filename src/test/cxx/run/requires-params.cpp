// std: c++20
// a trailing requires requires { x.base() != y.base(); } (libc++'s reverse_iterator !=): the parameters in scope
template <class T> struct R { T b; T base() const { return b; } };
template <class A, class B> bool ne(const R<A> &x, const R<B> &y) requires requires { x.base() != y.base(); } { return x.base() != y.base(); }
template <class A> int one(const R<A> &x) requires requires { x.base(); } { return 1; }
int main() { R<int> a{1}, b{2}; return ne(a, b) && one(a) == 1 ? 0 : 1; }
