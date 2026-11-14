// nullptr passed to deduced and std::nullptr_t parameters (by value and by forwarding reference)
typedef decltype(nullptr) nullptr_t;
struct P { bool a; int *b; };
struct J { int k; J(nullptr_t = nullptr) : k(7) {} };
struct H {
  int n = 0;
  template <class V> P handle_value(V &&v, const bool skip = false) { J j(static_cast<V &&>(v)); n += skip ? 0 : j.k; return {true, nullptr}; }
  template <class V> int *hv2(V &&v) { n++; return v; }
  bool null() { handle_value(nullptr); return hv2(nullptr) == nullptr; }
};
int main() { H h; J j(nullptr); return h.null() && h.n == 8 && j.k == 7 ? 0 : 1; }
