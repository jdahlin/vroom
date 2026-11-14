// nlohmann's detector idiom: a template template parameter in a specialization's pattern, matched by substitution.
template<class T> T &&dv() noexcept;
template<class...> struct make_void { typedef void type; };
template<class... Ts> using void_t = typename make_void<Ts...>::type;
struct nonesuch { };
struct yes { static constexpr bool value = true; }; struct no { static constexpr bool value = false; };
template<class A, class B> struct same : no { };
template<class A> struct same<A, A> : yes { };
template<class Default, class AlwaysVoid, template<class...> class Op, class... Args>
struct detector { using value_t = no; using type = Default; };
template<class Default, template<class...> class Op, class... Args>
struct detector<Default, void_t<Op<Args...>>, Op, Args...> { using value_t = yes; using type = Op<Args...>; };
template<template<class...> class Op, class... Args> using is_detected = typename detector<nonesuch, void, Op, Args...>::value_t;
template<template<class...> class Op, class... Args> using detected_t = typename detector<nonesuch, void, Op, Args...>::type;
template<class Expected, template<class...> class Op, class... Args> using is_detected_exact = same<Expected, detected_t<Op, Args...>>;
template<class T> using boolean_function_t = decltype(dv<T &>().boolean(dv<bool>()));
template<class T, class S> using string_function_t = decltype(dv<T &>().string(dv<S &>()));
struct Sax { bool boolean(bool v); bool string(long &s); int number(int); };
struct Other { };
static_assert(is_detected<boolean_function_t, Sax>::value, "1");
static_assert(!is_detected<boolean_function_t, Other>::value, "2");
static_assert(is_detected_exact<bool, boolean_function_t, Sax>::value, "3");
static_assert(is_detected_exact<bool, string_function_t, Sax, long>::value, "4");
static_assert(!is_detected_exact<bool, string_function_t, Other, long>::value, "5");
