/* [abi:expressions] sp: a pack expansion among the arguments of a call in a
   return type's decltype */
template <class T> T &&declval();
template <class F, class... A>
decltype(declval<F>()(declval<A>()...)) call(F f, A... a) { return f(a...); }
int g(int x, int y) { return x + y; }
int main() { return call(g, 1, 2) - 3; }
