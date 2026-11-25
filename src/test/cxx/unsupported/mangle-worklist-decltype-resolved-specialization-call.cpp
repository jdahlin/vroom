/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): declval<L>() of a class's parameter: a call of the resolved specialization, L_Z <encoding> E.
   clang: _ZN2dd6detail1EIiE2opIiEEDTeqclL_ZNS0_7declvalIiEEOT_vEEcl7declvalIS5_EEES6_ */
namespace dd { namespace detail {
template <class T> T&& declval();
struct Result { bool ok; Result(bool b = false) : ok(b) {} };
} }
namespace dd { namespace detail {
template <class L> struct E {
  L lhs;
  template <class R> auto op(R&& r) -> decltype(declval<L>() == declval<R>()) { return dd::detail::declval<decltype(declval<L>())>(); }
};
} }
int main() { dd::detail::E<int> e{1}; e.op(2); return 0; }
