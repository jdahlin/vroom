/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): !bool(F::value) && sizeof...(R) != 0 in a member alias's argument: sZT0_, the alias's parameter position.
   clang: _Z1fIPiTnN2enIXsr3AndI4ImplIXaantcvbsr4sameIT_S0_EE5valuenesZT0_Li0EEEEE5valueEiE4typeELi0EEiS3_ */
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
template <class F, class... R> using Or7 = Impl<!bool(F::value) && sizeof...(R) != 0>;
template <class O, en_t<And<Or7<same<O, int*>, yes > >::value, int> = 0> int f(O) { return 1; }
int main() { int a; return f(&a) - 1; }
