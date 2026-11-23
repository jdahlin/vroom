/* an alias-declaration with a GNU attribute that changes nothing (libc++'s _LIBCPP_DEPRECATED_IN_CXX17) */
struct S {
  using argument_type __attribute__((__deprecated__)) = int;
  using result_type [[deprecated]] = long;
};
int main() {
  S::argument_type a = 2;
  S::result_type b = 3;
  if (a + b != 5)
    return 1;
  return 0;
}
