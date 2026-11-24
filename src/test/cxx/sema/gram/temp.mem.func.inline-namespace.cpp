// [namespace.memdef]/3: a member of a class template of an inline namespace, defined in the enclosing namespace
inline namespace X {
inline namespace Y {
template <class T> struct C { int f(); };
}
template <class T> int C<T>::f() { return sizeof(T); }
}
int g() { return C<int>().f(); }
