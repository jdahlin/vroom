// [temp.variadic]/10 empty folds; explicit instantiation of a function template with an empty non-type pack: its template-param-decl (TpTn type) in the mangled name
template <bool... B> bool all() { return (B && ...); }
template <bool... B> bool any() { return (B || ...); }
template <int... N> int sum() { return (N + ... + 0); }
template <int... N> int last() { return (0, ..., N); }
template <typename T, T... N> T total() { return (N + ... + T(0)); }
template <int A, int... N> int head() { return A + (N + ... + 0); }
template <int... N> void touch(int *p) { *p += sizeof...(N); }
template bool all<>();
template bool any<>();
template int sum<>();
template int last<>();
template void touch<>(int *);
int main() {
  int c = 0;
  touch<>(&c);
  touch<7, 8>(&c);
  return all<>() && !all<true, false>() && !any<>() && sum<>() == 0 && sum<1, 2, 3>() == 6 && total<int, 1, 2>() == 3 && head<1, 2>() == 3 && c == 2 ? 0 : 1;
}
