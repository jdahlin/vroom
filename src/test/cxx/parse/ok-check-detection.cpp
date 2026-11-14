// The detection idiom: a specialization whose pattern is valid only for some types, matched by
// substituting the deduced parameters.  vcc evaluates each static_assert as true, clang accepts.
template<class...> using void_t = void;
template<class T> T &&dv() noexcept;
struct A { int size() const; typedef long value_type; int *operator->() const; };
struct B { };

template<class T, class = void> struct has_size { static constexpr bool value = false; };
template<class T> struct has_size<T, decltype((void)dv<const T &>().size())> { static constexpr bool value = true; };

template<class T, class = void> struct has_value_type { static constexpr bool value = false; };
template<class T> struct has_value_type<T, void_t<typename T::value_type>> { static constexpr bool value = true; };

template<class T, class = void> inline const bool has_arrow_v = false;
template<class T> inline const bool has_arrow_v<T, decltype((void)dv<const T &>().operator->())> = true;

template<class T, class = void> struct elem { typedef void type; };
template<class T> struct elem<T, void_t<typename T::value_type>> { typedef typename T::value_type type; };

static_assert(has_size<A>::value && !has_size<B>::value && !has_size<int>::value, "");
static_assert(has_value_type<A>::value && !has_value_type<B>::value, "");
static_assert(has_arrow_v<A> && !has_arrow_v<B> && !has_arrow_v<int>, "");
static_assert(sizeof(elem<A>::type) == 8, "");

// overloads made exclusive by it
template<bool, class T = void> struct en { };
template<class T> struct en<true, T> { typedef T type; };
template<class P, typename en<has_arrow_v<P>, int>::type = 0> char  addr(const P &);
template<class P, typename en<!has_arrow_v<P>, int>::type = 0> long addr(const P &);
void use(A a, B b) { static_assert(sizeof(addr(a)) == 1 && sizeof(addr(b)) == 8, ""); }

// a pattern shorter than the parameter list: the rest are the primary's default arguments
template<class T, class U, class = void> inline const bool same_ptr_v = false;
template<class T> inline const bool same_ptr_v<T *, T *> = true;
template<class T, class = void> struct is_ptr { static constexpr bool value = false; };
template<class T> struct is_ptr<T *> { static constexpr bool value = true; };
static_assert(same_ptr_v<int *, int *> && !same_ptr_v<int *, long *> && !same_ptr_v<int, int>, "");
static_assert(is_ptr<char *>::value && !is_ptr<char>::value, "");

// a pattern with template template parameters (nlohmann's is_basic_json)
namespace tt {
template<class T> struct alloc {};
template<class T, class A = alloc<T>> struct vec { T *p; };
template<template<class U, class... Args> class Arr = vec, class Num = long, template<class U> class Al = alloc> class bj;
template<class> struct is_bj { static constexpr bool value = false; };
template<template<class, class...> class Arr, class Num, template<class> class Al>
struct is_bj<bj<Arr, Num, Al>> { static constexpr bool value = true; typedef Num num; };
template<template<class, class...> class Arr, class Num, template<class> class Al> class bj { Arr<Num> a; };
static_assert(is_bj<bj<>>::value && is_bj<bj<vec, char>>::value && !is_bj<vec<int>>::value && !is_bj<int>::value, "");
static_assert(sizeof(is_bj<bj<vec, char>>::num) == 1, "");
}
// typename T::x with T not a class is a failed substitution
template<class T, class = void> struct reloc { static constexpr bool value = false; };
template<class T> struct reloc<T, void_t<typename T::value_type>> { static constexpr bool value = true; };
static_assert(!reloc<int>::value && !reloc<int *>::value && reloc<A>::value, "");
