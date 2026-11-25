// [abi:mangle.template-arg], [temp.alias]/2: alias templates used as template arguments of a
// value-dependent enable_if in a function template's declaration (the rungs of libc++'s
// __wrap_iter converting constructor: _And<..., _Or<...>>): an alias of a parameter, of a pack,
// whose type-id is a class template-id, a member alias of a dependent specialization, F::value of
// a name the alias bound (sr <simple-id> E), !bool(...) (nt cv b), a pack expansion of a bound pack
// among a member alias's arguments; symbols = clang's.  The rungs clang mangles otherwise or vcc
// refuses are in src/test/cxx/unsupported/mangle-alias-ladder-worklist.cpp

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

namespace nr01 {
template <class T> using id = T;
template <class O, en_t<id<same<O, int*> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr01

namespace nr02 {
template <class T> using id = T;
template <class O, en_t<And<id<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr02

namespace nr04 {
template <class... T> using all = And<T...>;
template <class O, en_t<And<all<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr04

namespace nr05 {
template <class T> using wrap1 = holder<T>;
template <class O, en_t<And<wrap1<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr05

namespace nr06 {
template <class T> using inner = typename holder<T>::type;
template <class O, en_t<And<inner<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr06

namespace nr10 {
template <bool> struct Impl { template <class R, class...> using Result = R; };
template <class F, class... Rest> using Or2 = typename Impl<!bool(F::value)>::template Result<F, Rest...>;
template <class O, en_t<And<Or2<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr10

namespace nr10b {
template <bool> struct Impl { template <class R, class...> using Result = R; };
template <class F, class... Rest> using Or2 = typename Impl<F::value>::template Result<F, Rest...>;
template <class O, en_t<And<Or2<same<O, int*>, yes> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr10b

namespace nr10c {
template <bool> struct Impl { template <class R, class...> using Result = R; };
template <class F, class... Rest> using Or2 = typename Impl<F::value>::template Result<F, Rest...>;
template <class O, en_t<And<Or2<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr10c

namespace nr11 {
template <bool> struct Impl { template <class R, class...> using Result = R; };
template <class F> using Or3 = typename Impl<F::value>::template Result<F>;
template <class O, en_t<And<Or3<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr11

namespace nr12 {
template <bool> struct Impl {};
template <class F> using Or4 = Impl<F::value>;
template <class O, en_t<And<Or4<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12

namespace nr12a {
template <bool> struct Impl {};
template <class F> using Or4 = Impl<F::value>;
template <class O> int g(O, Or4<same<O, int*> >) { return 1; }
int run() { int a; return g(&a, Impl<true>()) - 1; }
} // namespace nr12a

namespace nr12b {
template <bool> struct Impl {};
template <bool B> using I2 = Impl<B>;
template <class O, en_t<And<I2<same<O, int*>::value> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12b

namespace nr12c {
template <bool> struct Impl {};
template <class O, en_t<And<Impl<same<O, int*>::value> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12c

namespace nr12d {
template <bool> struct Impl {};
template <class O, en_t<And<Impl<!same<O, int*>::value> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12d

namespace nr12e {
template <bool> struct Impl {};
template <class O, en_t<And<Impl<!bool(same<O, int*>::value)> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12e

namespace nr12f {
template <bool> struct Impl {};
template <class F> using Or4 = Impl<F::value>;
template <class O, en_t<And<Or4<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12f

namespace nr12h {
template <class> struct Impl {};
template <class F> using Or4 = Impl<typename F::type>;
template <class O, en_t<And<Or4<holder<O> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr12h

namespace nr13 {
template <bool> struct Impl {};
template <class F> using Or5 = Impl<!bool(F::value)>;
template <class O, en_t<And<Or5<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr13

namespace nr16 {
template <bool> struct Impl { template <class R> struct Res { static constexpr bool value = true; }; };
template <class F> using Or8 = typename Impl<F::value>::template Res<F>;
template <class O, en_t<And<Or8<same<O, int*> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr16

namespace nr17 {
template <class O, en_t<And<yes, same<O, int*> >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr17

namespace nr18 {
template <class F> using Or9 = typename F::type;
template <class O, en_t<And<Or9<holder<O> > >::value, int> = 0> int f(O) { return 1; }
int run() { int a; return f(&a) - 1; }
} // namespace nr18

int main() {
  int failed = 0;
  failed += nr01::run() != 0;
  failed += nr02::run() != 0;
  failed += nr04::run() != 0;
  failed += nr05::run() != 0;
  failed += nr06::run() != 0;
  failed += nr10::run() != 0;
  failed += nr10b::run() != 0;
  failed += nr10c::run() != 0;
  failed += nr11::run() != 0;
  failed += nr12::run() != 0;
  failed += nr12a::run() != 0;
  failed += nr12b::run() != 0;
  failed += nr12c::run() != 0;
  failed += nr12d::run() != 0;
  failed += nr12e::run() != 0;
  failed += nr12f::run() != 0;
  failed += nr12h::run() != 0;
  failed += nr13::run() != 0;
  failed += nr16::run() != 0;
  failed += nr17::run() != 0;
  failed += nr18::run() != 0;
  return failed;
}
