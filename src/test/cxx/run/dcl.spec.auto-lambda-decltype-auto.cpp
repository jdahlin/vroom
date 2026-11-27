/* [dcl.spec.auto]/3: a lambda's -> decltype(auto) (libc++'s
   uninitialized_move). */
template <class T> T &deref(T *p) { return *p; }
int main(){ int x = 1; auto f = [](auto &&it) -> decltype(auto) { return deref(it); }; int *p = &x; f(p) = 2; auto g = [&]() -> decltype(auto) { return (x); }; g()++; return x - 3; }
