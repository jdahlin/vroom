// an enumerator (of an unnamed enum in a class) as the argument of an int or bool template parameter: its value,
// X<N::value> is X<1> (json's is_iterator_of_multibyte<T>::value picking the wide string input adapter)
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void> using enable_if_t = typename enable_if<B, T>::type;
template <class T> struct traits { typedef T value_type; };
template <class T> struct traits<T *> { typedef T value_type; };
template <class T> struct multi { using value_type = typename traits<T>::value_type; enum { value = sizeof(value_type) > 1 }; };
template <class I, class = void> struct fac { static int k() { return 1; } };
template <class I> struct fac<I, enable_if_t<multi<I>::value>> { static int k() { return 2; } };
static_assert(multi<const wchar_t *>::value, "");
int main0() { return fac<const wchar_t *>::k() == 2 && fac<const char *>::k() == 1 ? 0 : 1; }

template <bool B> struct B2 { static int k() { return B ? 2 : 1; } };
template <int I> struct I2 { static int k() { return I; } };
template <class T> struct M { enum { value = sizeof(T) > 1 }; };
struct N { enum { value = 1 }; };
template <class T> int viaT() { return B2<M<T>::value>::k(); }
int main() { return main0() + (B2<N::value>::k() != 2) + (I2<N::value>::k() != 1) + (viaT<int>() != 2) + (viaT<char>() != 1); }
