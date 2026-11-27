/* [abi:expressions], [impl.clang]: the constraints of a member template of a
   class template specialization are mangled unsubstituted: a concept of a
   namespace by its nested-name (a substitution candidate before its
   template-args), the template's own parameters in an alias's expansion by
   their level (TL0__), and a typedef of the class that depends on no
   parameter by its type. */
extern "C" int printf(const char *, ...);
namespace N {
template<class A, class B> constexpr bool same = __is_same(A, B);
template<class A, class B> constexpr bool conv = __is_convertible(A, B);
template<class T> concept it = true;
template<class E, class I> concept sent = true;
template<class T> concept prim = true;
template<class T> struct traits { using value_type = char; };
template<class T> struct rd { using value_type = char; };
template<bool B, class A, class C> struct cond { using type = A; };
template<class A, class C> struct cond<false, A, C> { using type = C; };
template<class T> using value_t = typename cond<prim<traits<__remove_cvref(T)>>, rd<__remove_cvref(T)>, traits<__remove_cvref(T)>>::type::value_type;
template<class C> struct S {
  using size_type = unsigned long;
  template<it I, sent<I> E> requires(same<value_t<I>, C> && !conv<E, size_type>) S(I, E) : n(1) {}
  template<sent<C> A, sent<C> B> S(A, B, int) : n(2) {}
  int n;
};
}

int main()
{
    const char *a = "x";
    N::S<char> s(a, a);
    N::S<char> t(a, a, 0);
    printf("%d %d\n", s.n, t.n);
    return s.n != 1 || t.n != 2;
}
