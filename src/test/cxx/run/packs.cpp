// function templates with parameter packs: one parameter per element, args... forwarded, sizeof...,
// empty packs; their names are clang's (DpT_, J...E)
template <class... T> int count(T &...) { return sizeof...(T); }
template <class F, class... A> int call(F f, A... a) { return f(a...); }
template <class... A> int fwd(int (*g)(int, long), A &&...a) { return g(static_cast<A &&>(a)...); }
static int add(int x, long y) { return x + (int)y; }
int main() { int a = 1; long b = 2; return count(a, b) == 2 && call(add, 4, 5L) == 9 && fwd(add, 3, b) == 5 && count() == 0 ? 0 : 1; }
