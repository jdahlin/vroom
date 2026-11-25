/* Mangling worklist (json's ladders): the same alias as a parameter type: clang keeps sizeof...(R) as sZT0_; vcc's deduction refuses the call.
   clang: _Z1gIPiEiT_4ImplIXnesZT0_Li0EEE */
template <bool B> struct bc { static constexpr bool value = B; };
typedef bc<true> yes;
typedef bc<false> no;
template <bool C, class R = void> struct en {};
template <class R> struct en<true, R> { typedef R type; };
template <bool C, class R = void> using en_t = typename en<C, R>::type;
template <class A, class B> struct same : no {};
template <class A> struct same<A, A> : yes {};
template <class... T> struct And { static constexpr bool value = true; };
template <class T> struct holder { typedef T type; };
template <bool> struct Impl {};
template <class F, class... R> using Or6 = Impl<sizeof...(R) != 0>;
template <class O> int g(O, Or6<same<O, int*>, yes>) { return 1; }
int main() { int a; return g(&a, Impl<true>()) - 1; }
