// [conv.ptr]/3: a pointer to a complete class converts only to a pointer to one of its bases; a
// call that needs another class's pointer is a substitution failure (libc++'s __has_destroy_v:
// allocator<__tree_node<...>>().destroy(pair<...>*))
template <class T> T &&dv();
template <class A, class B> struct same { static constexpr bool v = false; };
template <class A> struct same<A, A> { static constexpr bool v = true; };
template <bool B, class T = void> struct en {};
template <class T> struct en<true, T> { using type = T; };
struct N { int x; };
struct V { int y; };
struct D : N {};
struct A { void destroy(N *) {} };
template <class Al, class P, class = void> constexpr bool has_destroy = false;
template <class Al, class P>
constexpr bool has_destroy<Al, P, decltype((void)dv<Al>().destroy(dv<P>()))> = true;
template <class P, class = void> struct pick { static constexpr int v = 2; };
template <class P> struct pick<P, typename en<same<decltype(A().destroy(dv<P>())), void>::v>::type> {
  static constexpr int v = 1;
};
int main() {
  return has_destroy<A, N *> && !has_destroy<A, V *> && has_destroy<A, D *> && pick<V *>::v == 2 &&
                 pick<D *>::v == 1
             ? 0
             : 1;
}
