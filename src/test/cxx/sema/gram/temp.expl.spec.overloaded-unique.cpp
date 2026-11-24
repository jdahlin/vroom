// [temp.deduct.decl]/1: an explicit specialization of one of overloaded function templates, the one the declared type deduces
template <class T> void f(T);
template <class T> void f(T *);
template <> void f(int);
template <class T> void g(T, int);
template <class T, class U> void g(T *, U);
template <> void g(char *, long);
template <class T> void h(T);
template <class T> void h(T, T);
template <> void h(int, int);
void use() { f(1); g((char *)0, 1L); h(1, 2); }
