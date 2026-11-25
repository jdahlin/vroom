/* cxx-unsupported: lowering */
/* Mangling worklist (json's ladders): decltype(void(r), Result{}): Result{} is tl <type> <its default argument Lb0E> E in clang.
   clang: _ZN2dd6detail1EIiE2opIiEEDTcmcvvfp_tlNS0_6ResultELb0EEEOT_ */
namespace dd { namespace detail {
template <class T> T&& declval();
struct Result { bool ok; Result(bool b = false) : ok(b) {} };
} }
namespace dd { namespace detail {
template <class L> struct E {
  L lhs;
  template <class R> auto op(R&& r) -> decltype(void(r), Result{}) { return {}; }
};
} }
int main() { dd::detail::E<int> e{1}; e.op(2); return 0; }
