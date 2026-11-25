// [temp.inst]/4: an out-of-class member defined after its specialization is
// instantiated only if its definition is required.
template<class T> struct X { void unused(); int used(); };
X<int> x;
template<class T> void X<T>::unused() { T::missing(); }
template<class T> int X<T>::used() { return 17; }
static_assert(sizeof(x) > 0);
int use() { return x.used(); }
