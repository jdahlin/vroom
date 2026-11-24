// [temp.class]/1, [temp.class.spec]/1: a class template declared with
// visibility("hidden") (libc++'s _LIBCPP_HIDDEN) and then partially
// specialized, as <__charconv/traits.h> does.
template <bool C, typename T = void> struct enable { };
template <typename T> struct enable<true, T> { typedef T type; };

template <typename T, typename = void>
struct __attribute__((visibility("hidden"))) traits_base;

template <typename T>
struct __attribute__((visibility("hidden")))
    traits_base<T, typename enable<(sizeof(T) <= 4)>::type> {
  static int width() { return 32; }
};

template <typename T>
struct __attribute__((visibility("hidden")))
    traits_base<T, typename enable<(sizeof(T) > 4)>::type> {
  static int width() { return 64; }
};

int main()
{
  if (traits_base<char>::width() != 32)
    return 1;
  if (traits_base<long>::width() != 64)
    return 2;
  return 0;
}
