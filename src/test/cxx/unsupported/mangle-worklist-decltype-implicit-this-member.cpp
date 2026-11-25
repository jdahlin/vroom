/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): lhs, a member of *this, in a decltype: dt de fpT <name>.
   clang: _ZN2dd6detail1EIiE2opIiEEDTcmcvveqdtdefpT3lhsfp_tlNS0_6ResultELb0EEEOT_ */
namespace dd { namespace detail {
template <class T> T&& declval();
struct Result { bool ok; Result(bool b = false) : ok(b) {} };
} }
namespace dd { namespace detail {
template <class L> struct E {
  L lhs;
  template <class R> auto op(R&& r) -> decltype(void(lhs == r), Result{}) { return {}; }
};
} }
int main() { dd::detail::E<int> e{1}; e.op(2); return 0; }
