/* [temp.alias]/2: an alias specialization whose type is cv-qualified
   (id<const T>) is that type, in a type-specifier and in a partial
   specialization's arguments; [temp.alias]/3 (CWG 1558): the arguments
   the type does not keep are substituted when matching, a failure no
   match; [temp.deduct.type]/1: T deduced from const T (libc++'s
   __tuple/tuple_size.h) */
template <class T, class...> using id = T;
template <bool B> struct enable_if {};
template <> struct enable_if<true> { using type = void; };
template <class T> struct is_volatile { static const bool value = false; };
template <class T> struct is_volatile<volatile T> { static const bool value = true; };
template <class T> struct ts { static const int none = 1; };
template <class T> struct ts<id<const T, typename enable_if<!is_volatile<T>::value>::type, decltype(ts<T>::value)>> { static const int value = ts<T>::value + 100; };
struct X {};
template <> struct ts<X> { static const int value = 7; };
struct Y {};
template <class A, class B> struct same { static const bool value = false; };
template <class A> struct same<A, A> { static const bool value = true; };
using A = id<const int>;
static_assert(same<A, const int>::value, "");
int main() {
  if (ts<const X>::value != 107) return 1;
  if (ts<const Y>::none != 1) return 2;
  if (ts<const volatile X>::none != 1) return 4;
  const A a = 3;
  return a - 3;
}
