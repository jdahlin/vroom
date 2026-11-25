// operator<(a, b) written as a call: the function templates of that name and its non-template functions are
// candidates together (nlohmann::basic_json calls operator<(value_t, value_t) beside json_pointer's template)
namespace n {
enum class vt { a, b };
template <class A, class B> struct P { int t; };
template <class A, class B>
inline bool operator<(const P<A, B> &l, const P<A, B> &r) noexcept { return l.t < r.t; }
inline bool operator<(vt l, vt r) noexcept { return (int)l < (int)r; }
struct J {
  vt k;
  friend bool operator<(const J &l, const J &r) noexcept {
    return operator<(l.k, r.k);
  }
};
}
int main() { n::J a{n::vt::a}, b{n::vt::b}; return a < b ? 0 : 1; }
