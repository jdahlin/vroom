template <class T> struct S { void f(); };
template <class T> void S<T>::f() {}
S<int> s;
template <> void S<int>::f() {}
void h() { s.f(); }
