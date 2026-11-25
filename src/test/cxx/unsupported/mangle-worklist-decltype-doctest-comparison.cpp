/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): doctest's decltype(void(declval<L>() == declval<R>()), Result{}).
   clang: _ZN2dd6detail1EIiE2opIiEEDTcmcvveqclL_ZNS0_7declvalIiEEOT_vEEcl7declvalIS5_EEtlNS0_6ResultELb0EEES6_ */
namespace dd { namespace detail {
template <class T> T&& declval();
struct Result { bool ok; Result(bool b = false) : ok(b) {} };
} }
namespace dd { namespace detail {
template <class L> struct E {
  L lhs;
  template <class R> auto op(R&& r) -> decltype(void(declval<L>() == declval<R>()), Result{}) { return {}; }
};
} }
int main() { dd::detail::E<int> e{1}; e.op(2); return 0; }
