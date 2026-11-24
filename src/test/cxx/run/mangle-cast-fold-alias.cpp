template<bool B,class T=void> struct ei{};
template<class T> struct ei<true,T>{typedef T type;};
template<bool B,class T=void> using eit = typename ei<B,T>::type;
template<class T> struct isa{static const bool value=true;};
float pi(float); double pi(int);
template <class... A>
using pt = decltype((eit<(isa<A>::value && ...)>)0, (pi(A()) + ...));
template <class A1, class A2> pt<A1,A2> cs(A1 x, A2 y) { return 0; }
void f(){ cs(1,2); }
int main() { f(); return 0; }
