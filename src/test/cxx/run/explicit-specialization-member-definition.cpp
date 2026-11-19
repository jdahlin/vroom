/* [temp.expl.spec]/7: out-of-class member definitions of explicit
   specializations, their parameters naming the specialization's types
   (libc++'s char_traits<char16_t>::compare) */
typedef decltype(sizeof 0) size_t;
template <class T> struct ct {};
template <> struct ct<char> {
  typedef char char_type;
  static int f(const char_type*, size_t) noexcept;
};
template <> struct ct<short> {
  typedef short char_type;
  static int f(const char_type*, size_t) noexcept;
};
[[nodiscard]] inline int ct<char>::f(const char_type* a, size_t n) noexcept {
  return a[0] + (int)n;
}
inline int ct<short>::f(const char_type* a, size_t n) noexcept {
  return a[0] * 10 + (int)n;
}
int main()
{
  const char c[] = {5};
  const short s[] = {7};
  if (ct<char>::f(c, 2) != 7)
    return 1;
  if (ct<short>::f(s, 2) != 72)
    return 2;
  return 0;
}
