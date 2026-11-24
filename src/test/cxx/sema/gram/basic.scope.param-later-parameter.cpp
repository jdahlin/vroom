// [basic.scope.param]/1: a parameter's name is in scope from its point of
// declaration to the end of the function declarator: a later parameter's
// declaration finds it.
template <class T> void g(T x, decltype(x) y);
template <class T> void g(T x, decltype(x) y) {}
void h(int a, decltype(a) b, decltype(b) *c);
void k(int a, int (&b)[sizeof(a)]);
