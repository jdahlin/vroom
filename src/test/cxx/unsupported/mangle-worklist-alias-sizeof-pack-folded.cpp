/* Mangling worklist (json's ladders): Impl<sizeof...(A) != 0> with A bound to a pack of one: clang folds it to Impl<true> and the whole argument (TniLi0E).
   clang: _Z1fIPiTniLi0EEiT_ */
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
template <class... A> using Or1 = typename Impl<sizeof...(A) != 0>::template Result<no, A...>;
template <class O, en_t<And<Or1<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int main() { int a; return f(&a) - 1; }
