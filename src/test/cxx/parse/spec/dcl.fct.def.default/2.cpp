// [dcl.fct.def.default] Example 2 (paragraph 6), docs/cxx-spec/dcl/dcl.fct.def.default.md
// needs: P5
// status: well-formed (as far as the example says)

struct trivial {
  trivial() = default;
  trivial(const trivial&) = default;
  trivial(trivial&&) = default;
  trivial& operator=(const trivial&) = default;
  trivial& operator=(trivial&&) = default;
  ~trivial() = default;
};

struct nontrivial1 {
  nontrivial1();
};
nontrivial1::nontrivial1() = default;   // not first declaration
