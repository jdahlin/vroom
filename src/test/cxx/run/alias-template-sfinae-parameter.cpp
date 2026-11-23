/* [temp.alias]/4 (CWG 1558): void_t<typename U::type> in a function
   template's parameter is a substitution failure for a U without type: the
   detection idiom of libc++'s __has_iterator_typedefs */
template <class...> using void_t = void;
struct yes {
  char c;
};
struct no {
  char c[2];
};
struct A {
  typedef int type;
};
struct B {};

template <class U> no test(...);
template <class U> yes test(void_t<typename U::type> * = nullptr);

template <class T> struct has_type {
  template <class U> static no probe(...);
  template <class U> static yes probe(void_t<typename U::type> * = nullptr);
  static const bool value = sizeof(decltype(probe<T>(nullptr))) == sizeof(yes);
};

int main()
{
  if (sizeof(test<A>(nullptr)) != sizeof(yes))
    return 1;
  if (sizeof(test<B>(nullptr)) != sizeof(no))
    return 2;
  if (!has_type<A>::value)
    return 3;
  if (has_type<B>::value)
    return 4;
  return 0;
}
