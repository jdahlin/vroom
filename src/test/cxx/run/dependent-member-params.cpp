// a member template whose parameter's type is a member type of its parameter (typename J::int_t): its
// signature does not parse with placeholders, so it is mangled from its tokens (NT_5int_tE), as clang does
enum class kind { i, d };
template <kind K> struct ctor;
template <> struct ctor<kind::i> {
  template <class J> static void construct(J &j, typename J::int_t v) noexcept { j.k = kind::i; j.i = v; }
};
template <> struct ctor<kind::d> {
  template <class J> static void construct(J &j, const typename J::dbl_t &v) noexcept { j.k = kind::d; j.d = v; }
};
struct J { typedef long int_t; typedef double dbl_t; kind k; long i = 0; double d = 0; };
int main() { J j; ctor<kind::i>::construct(j, 4L); ctor<kind::d>::construct(j, 1.5); return j.i == 4 && j.d == 1.5 && j.k == kind::d ? 0 : 1; }
