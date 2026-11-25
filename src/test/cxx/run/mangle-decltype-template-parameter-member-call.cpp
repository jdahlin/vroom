// [abi:expressions] T::name in a decltype of a function template's signature, T a template type
// parameter: sr <template-param> <source-name> (libc++'s __unwrap_iter:
// decltype(_Impl::__unwrap(std::declval<_Iter>())))
namespace s { template <class T> T &&dv(); }
template <class I> struct impl { static I un(I i) { return i; } };
template <class I, class Im = impl<I>> decltype(Im::un(s::dv<I>())) unw(I i) { return Im::un(i); }
int main() {
  int a = 3;
  return *unw(&a) == 3 ? 0 : 1;
}
