// [dcl.init]/17.6.3, [meta.unary.prop]: __is_assignable(S&, const U) of a class S with no
// constructor and no base from a class U none of whose conversion functions yields S is false
// (json's from_json overloads test is_assignable<identity_tag<T>&, const std::string>)
template <class T> struct identity_tag {};
struct view {};
struct text {
  int n = 1;
  operator view() const { return {}; }
};
struct S {
  int a;
};
static_assert(!__is_assignable(S &, const text), "");
static_assert(!__is_assignable(identity_tag<int> &, const text), "");
static_assert(__is_assignable(view &, const text), "");
static_assert(__is_assignable(S &, const S &), "");
template <class T, bool = __is_assignable(T &, const text)> struct pick {
  static constexpr int value = 1;
};
template <class T> struct pick<T, true> {
  static constexpr int value = 2;
};
int main() { return pick<S>::value + pick<view>::value == 3 ? 0 : 1; }
