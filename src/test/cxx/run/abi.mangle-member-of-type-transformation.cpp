/* [abi:mangle.name]: typename __remove_const(T)::name, a member of a type
   transformation, as a prefix (std::map's insertion) */
template <class T> struct same { static const bool value = true; };
template <class T, class U> struct two { static const bool value = true; };
template <bool B, class T> struct enable {};
template <class T> struct enable<true, T> { typedef T type; };
struct P { typedef int first_type; };
template <class T> struct box {};
template <class T> using bare = __remove_const(__remove_reference_t(T));
template <class K, class A, typename enable<two<__remove_const(typename bare<A>::first_type), K>::value, int>::type = 0>
int f(A &&, K, bare<A> *, K *) { return 0; }
int main() { P p; return f(p, 1, (P *)0, (int *)0); }
