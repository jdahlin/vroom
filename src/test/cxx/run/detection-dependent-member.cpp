// [temp.deduct.type]/5.1: a dependent member in a partial specialization
// is non-deduced; substitute the template-template argument once known.
struct yes { static constexpr bool value = true; };
struct no { static constexpr bool value = false; };
template<class T> T&& declval();
template<class...> struct make_void { using type = void; };
template<class... T> using void_t = typename make_void<T...>::type;
struct none {};
template<class D, class V, template<class...> class Op, class... Args>
struct detector { using value_t = no; };
template<class D, template<class...> class Op, class... Args>
struct detector<D, void_t<Op<Args...>>, Op, Args...> { using value_t = yes; };
template<template<class...> class Op, class... Args>
using detected = typename detector<none, void, Op, Args...>::value_t;
struct S { void append(const char*); };
template<class T, class A>
using append = decltype(declval<T&>().append(declval<A&&>()));
template<class T> using begin = decltype(declval<T&>().begin());
struct Range { int* begin(); };
static_assert(detected<append, S, const char*>::value);
static_assert(!detected<append, S, int>::value);
static_assert(detected<begin, Range>::value);
static_assert(!detected<begin, char[3]>::value);
int main() { return 0; }
