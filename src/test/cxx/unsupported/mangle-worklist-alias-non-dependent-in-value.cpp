/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): And<alias<X>> whose type-id is not dependent: clang evaluates the value (TniLi0E).
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
template <bool> struct Impl {};
template <class F> using Or6 = Impl<true>;
template <class O, en_t<And<Or6<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int main() { int a; return f(&a) - 1; }
