// [temp.expl.spec]/2, [namespace.memdef]/3: specializations declared in the enclosing namespace of the primary's inline namespace
inline namespace X {
inline namespace Y {
template <class T> struct C { static const int v = 0; };
}
template <> struct C<int> { static const int v = 1; };
template <class T> struct C<T *> { static const int v = 2; };
}
static_assert(C<char>::v == 0, "");
static_assert(C<int>::v == 1, "");
static_assert(C<int *>::v == 2, "");
template struct C<char>;
