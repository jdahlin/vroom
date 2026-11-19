// RUN: %clang_cc1 -std=c++20 %s
// [ext.clang] clang's type trait primitives (clang Language Extensions,
// Type Trait Primitives; src/frontend/cplusplus/ext/clang/type-traits.cpp):
// expression traits, type transformations, and a trait's keyword used as
// a name when no ( follows it (libstdc++'s __is_signed,
// __make_unsigned, ...).
static_assert(__is_same(int, int));
static_assert(!__is_same(int, long) && __is_integral(int));
static_assert(__is_constructible(int, long));
static_assert(__array_extent(int[3][4], 1) == 4);
static_assert(__array_rank(int[3][4]) == 2);
template <class T, class... A>
constexpr bool constructible = __is_constructible(T, A...);
static_assert(constructible<int, int>);
static_assert(__is_same(__remove_cv(const volatile int), int));
static_assert(__is_same(__make_unsigned(int), unsigned));
template <class T> using unsigned_of = __make_unsigned(T);
static_assert(__is_same(unsigned_of<long>, unsigned long));
__remove_reference_t(int &) plain = 1;

template <class T> struct __is_signed { static constexpr bool value = T(-1) < T(0); };
static_assert(__is_signed<int>::value && !__is_signed<unsigned>::value);
template <class T> struct __make_unsigned { using type = unsigned; };
static_assert(__is_same(__make_unsigned<int>::type, unsigned));

// [impl.clang] __builtin_va_list is a predeclared typedef-name.
typedef __builtin_va_list va_list_t;
__builtin_va_list predeclared_va_list;
void takes_va_list(__builtin_va_list *p, va_list_t q);

// [ext:clang_convertvector] __builtin_convertvector(vector, type-id), as
// libc++'s __algorithm/simd_utils.h converts a comparison's mask; the
// type-id's > ends a template-argument-list.
template <class T, unsigned long N>
using vec __attribute__((__ext_vector_type__(N))) = T;
template <class T, unsigned long N>
vec<long, N> as_mask(vec<T, N> v) { return __builtin_convertvector(v == v, vec<long, N>); }
