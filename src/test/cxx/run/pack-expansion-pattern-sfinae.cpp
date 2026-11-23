/* [temp.variadic]/5: a pack expansion whose pattern is a template-id of a
   dependent expression in a function template's return type (libc++'s
   __and_helper: __expand_to_true<__enable_if_t<_Pred::value>...>), a
   substitution failure of one element no candidate ([temp.deduct]/8);
   [temp.arg.explicit]/9: explicit arguments of a trailing pack; a
   template with an ellipsis parameter ([temp.deduct.call]/1) */
template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
template <bool B, class T = void>
using enable_if_t = typename enable_if<B, T>::type;
struct true_type { static const bool value = true; };
struct false_type { static const bool value = false; };
template <class...> using expand_to_true = true_type;
template <class... P> expand_to_true<enable_if_t<P::value>...> and_helper(int);
template <class...> false_type and_helper(...);
template <class... Ts> int count(...) { return sizeof...(Ts); }
int main()
{
    decltype(and_helper<true_type, false_type>(0)) a;
    decltype(and_helper<true_type>(0)) b;
    decltype(and_helper<>(0)) c;
    return a.value + 2 * b.value + 4 * c.value - 6 + count<int, char>(1, 2) - 2;
}
