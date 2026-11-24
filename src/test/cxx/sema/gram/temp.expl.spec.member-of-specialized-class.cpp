// [temp.expl.spec]/5: the members of an explicitly specialized class are defined without template<>
template <class T> struct A { void f(T); };
template <> struct A<char> { void f(char); };
void A<char>::f(char) {}
template <> void A<char>::f(char) {}
