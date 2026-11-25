// [temp.alias]/2, /4: an alias template's specialization kept for its dependent arguments (CWG
// 1558) whose type-id is not dependent (void_t<T>, an alias of box<int>) is mangled as that type, as
// clang does (vcc wrote the alias, P6void_tIJT_EE); symbols = clang's
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
template <class...> using void_t = void;
template <class T> using ptr = T*;
template <class T> struct box {};
template <class T> using boxed_int = box<int>;
template <class T> int f(void_t<typename T::type>*) { return 1; }
template <class T> int g(void_t<T>*, void_t<T, int>*) { return 1; }
template <class T> int h(boxed_int<T>, boxed_int<T*>, ptr<T>) { return 1; }
int main() { int i; return f<holder<int> >(0) + g<int>(0, 0) + h<int>(box<int>(), box<int>(), &i) - 3; }
