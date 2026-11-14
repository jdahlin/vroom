struct S { int v; };
S &lref(); S &&xref(); S val();
template<int N> struct A { char c[N]; };
template<class T> struct is_l { static constexpr int value = 1; };
template<class T> struct is_l<T&> { static constexpr int value = 2; };
template<class T> struct is_l<T&&> { static constexpr int value = 3; };
void g(S s, S *p) {
  A<is_l<decltype(s)>::value> a1;
  A<is_l<decltype((s))>::value + 10> a12;
  A<is_l<decltype(lref())>::value + 20> a22;
  A<is_l<decltype(xref())>::value + 30> a33;
  A<is_l<decltype(val())>::value + 40> a41;
  A<is_l<decltype(*p)>::value + 50> a52;
  A<is_l<decltype(s.v)>::value + 60> a61;
}
