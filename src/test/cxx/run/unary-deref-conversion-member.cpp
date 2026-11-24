struct Type { int v; };
template <bool B, class T = void> struct eif {};
template <class T> struct eif<true, T> { using type = T; };
template <class T> struct rm { using type = T; };
template <class T> struct rm<const T> { using type = T; };
template <class T> struct isv { static constexpr bool value = false; };
template <> struct isv<void> { static constexpr bool value = true; };
template <class T> struct rp {
  T* p = nullptr;
  constexpr rp(T* q) : p(q) {}
  template <typename U = T, typename Unused = typename eif<!isv<typename rm<U>::type>::value>::type>
  constexpr U& operator*() const { return *p; }
  constexpr T* operator->() const { return p; }
  constexpr operator T*() const { return p; }
  template <typename U> constexpr explicit operator U*() const { return static_cast<U*>(p); }
};
struct Sym {
  rp<const Type> fType;
  const Type& type() const { return *fType; }
  Sym(const Type* t) : fType(t) {}
};
int main() { Type t{3}; Sym s(&t); if (s.type().v != 3) return 1; return 0; }
