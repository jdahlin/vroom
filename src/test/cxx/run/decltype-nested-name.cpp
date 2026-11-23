/* [expr.prim.id.qual]/1: decltype-specifier :: as a nested-name-specifier,
   of a class (libc++'s decltype(__test<_Tp>(...))::value), also dependent */
struct yes {
  static const bool value = true;
  typedef int type;
  static int twice(int x) { return 2 * x; }
};
struct no {
  static const bool value = false;
  typedef long type;
};
yes probe(int);
no probe(...);

template <class T> struct has_int {
  static const bool value = decltype(probe(T()))::value;
  typedef typename decltype(probe(T()))::type type;
};

yes global;

int main()
{
  if (!decltype(probe(1))::value)
    return 1;
  if (decltype(probe("x"))::value)
    return 2;
  if (decltype(global)::twice(4) != 8)
    return 3;
  if (!has_int<int>::value)
    return 4;
  if (has_int<void *>::value)
    return 5;
  if (sizeof(has_int<int>::type) != sizeof(int))
    return 6;
  decltype(global)::type v = 3;
  if (v != 3)
    return 7;
  return 0;
}
