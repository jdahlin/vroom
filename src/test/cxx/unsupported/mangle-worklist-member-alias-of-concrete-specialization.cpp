/* Mangling worklist (json's ladders): typename C<true>::template Result<X> (a member alias template of a non-dependent specialization) in a template argument: the analysis refuses it.
   clang: _Z1fIPiTnN2enIXsr3AndI4sameIT_S0_EEE5valueEiE4typeELi0EEiS3_ */
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
template <bool> struct Impl { template <class R, class...> using Result = R; };
template <class O, en_t<And<typename Impl<true>::template Result<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int main() { int a; return f(&a) - 1; }
