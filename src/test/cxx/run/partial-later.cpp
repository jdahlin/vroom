// std: c++20
// a partial specialization declared after another was used: both still select (the first one's
// instances kept a pointer into the template's list, which declaring the second moved)
template <class T> struct PT { static constexpr int v = 0; };
template <class T> struct PT<T *> { static constexpr int v = 1; template <class U> using rebind = U *; };
int a = PT<const char *>::v + PT<char *>::v;
template <class T> struct W {};
template <class T> struct PT<W<T>> { static constexpr int v = 2; };
template <class T> struct X {};
template <class T> struct PT<X<T>> { static constexpr int v = 3; };
int main() {
  PT<void *>::rebind<int> p = nullptr;
  return (a == 2 && PT<const wchar_t *>::v == 1 && PT<W<int>>::v == 2 && PT<X<int>>::v == 3 && PT<int>::v == 0 && !p) ? 0 : 1;
}
