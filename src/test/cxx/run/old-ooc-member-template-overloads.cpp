// member templates of a class template defined outside it, two overloads with the same parameters told
// apart only by an enable_if value parameter (basic_string::__init for input and forward iterators)
template <bool B, class T = void> struct EI {};
template <class T> struct EI<true, T> { using type = T; };
template <class I> struct is_ptr { static constexpr bool value = false; };
template <class I> struct is_ptr<I *> { static constexpr bool value = true; };
template <class T> struct S {
  int n = 0;
  template <class I, typename EI<!is_ptr<I>::value, int>::type = 0> void init(I a, I b);
  template <class I, typename EI<is_ptr<I>::value, int>::type = 0> void init(I a, I b);
};
template <class T>
template <class I, typename EI<!is_ptr<I>::value, int>::type >
void S<T>::init(I a, I b) { n = 100 + (int)(b - a); }
template <class T>
template <class I, typename EI<is_ptr<I>::value, int>::type >
void S<T>::init(I a, I b) { n = (int)(b - a); }
int main() { S<char> s, t; const char *p = "abc"; s.init(p, p + 3); t.init(1L, 5L); return s.n == 3 && t.n == 104 ? 0 : 1; }
