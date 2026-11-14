// std: c++20
// Tn in member templates of class templates: the class's parameter written as its argument (S2_), an
// unqualified variable template (7is_al_vIT_E); no Tn where only the class's parameters are named (C2ILi0EE)
namespace std {
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void> using __enable_if_t = typename enable_if<B, T>::type;
template <class T, class U> struct is_same { static const bool value = false; };
template <class T> struct is_same<T, T> { static const bool value = true; };
template <class T> struct alloc_x {};
}
template <class T> inline constexpr bool is_al_v = true;
template <class A> struct bs {
  int k = 0;
  template <std::__enable_if_t<is_al_v<A>, int> = 0> bs(const A &) { k = 1; }
  template <class U, std::__enable_if_t<std::is_same<U, A>::value && is_al_v<U>, int> = 0> void f(U) { k++; }
};
template <class I, class S, std::__enable_if_t<is_al_v<I>, int> = 0> int g(I i, S s) { return i + s; }
int main() { std::alloc_x<char> a; bs<std::alloc_x<char>> b(a); b.f(a); return b.k == 2 && g(1, 2) == 3 ? 0 : 1; }
