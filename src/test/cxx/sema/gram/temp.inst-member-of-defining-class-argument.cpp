// [temp.inst]/2 and /4: a data member of type D<E> in E needs D<E> complete,
// which does not need E complete; D's member function bodies are
// instantiated only when used
template<typename T> struct D {
  int x;
  bool operator==(const D&) const { typename T::error e; return true; }
};
struct E {
  D<E> d;
  bool operator==(const E&) const = default;
};
static_assert(sizeof(E) == sizeof(int));
