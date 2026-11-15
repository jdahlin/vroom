// p->~T() of a class template's instance: its destructor's body instantiated (libc++'s __hash_node)
static int n;
inline void *operator new(__SIZE_TYPE__, void *p) throw() { return p; }
template <class T> struct X { T v; ~X() { ++n; } };
template <class T> void d(T *p) { p->~T(); }
int main() { X<int> a, b; X<int> *p = &a; p->~X(); d(&b); int k = n; new (&a) X<int>; new (&b) X<int>; return k == 2 ? 0 : 1; }
