// std: c++20
// two overloads told apart only by enable_if<cond> and enable_if<!cond> (allocator_traits::construct with and without
// the allocator's construct): their names Tn N ei IX <cond> E ... E, with T* and Args... in the condition (PT_, DpT0_), not one name
template <bool B, class T = void> struct ei { using type = T; };
template <class T> struct ei<false, T> {};
template <class A, class... B> inline constexpr bool hc = sizeof...(B) == 1;
template <class Al> struct AT {
  using at = Al;
  template <class T, class... Args, typename ei<hc<at, T *, Args...>, int>::type = 0> static int c(T *, Args &&...) { return 1; }
  template <class T, class... Args, typename ei<!hc<at, T *, Args...>, int>::type = 0> static int c(T *, Args &&...) { return 2; }
};
int f(int *p, int x) { return AT<long>::c(p, x) * 10 + AT<long>::c(p); }
int main() { int i = 0; return f(&i, 1) == 21 ? 0 : 1; }
