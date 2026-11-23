template <class T> struct S { void f(); void g(); static int v; };
template <class T> void S<T>::f() { T::nothing(); }
template <class T> void S<T>::g() {}
template <class T> int S<T>::v = T::nothing;
S<int> s;
void h() { s.g(); }
