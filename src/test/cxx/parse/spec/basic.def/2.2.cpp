// [basic.def] Example 2, part 2 (paragraph 3), docs/cxx-spec/basic/basic.def.md
// needs: P7 (std::string is not declared)
// status: well-formed (as far as the example says)

struct C {
  std::string s;
  C() : s() { }
  C(const C& x): s(x.s) { }
  C(C&& x): s(static_cast<std::string&&>(x.s)) { }
      //    : s(std::move(x.s))
  C& operator=(const C& x) { s = x.s; return *this; }
  C& operator=(C&& x) { s = static_cast<std::string&&>(x.s); return *this; }
      //                s = std::move(x.s); return *this;
  ~C() { }
};
