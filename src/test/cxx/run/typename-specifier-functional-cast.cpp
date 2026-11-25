// typename Q::name(args) and typename Q::name{args}: an explicit type conversion whose type is a
// typename-specifier (libc++'s basic_string::operator __self_view builds typename __self_view::__assume_valid())
struct tag {};
template <class V> struct view { struct assume : tag {}; typedef int value; };
template <class T> struct S {
  typedef view<T> self_view;
  int f() { typename self_view::assume a = typename self_view::assume(); (void)a; return typename self_view::value(); }
  int g() { return typename self_view::value{5}; }
};
int main() { S<char> s; return s.f() + s.g() - 5; }
