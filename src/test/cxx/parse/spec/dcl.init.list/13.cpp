// [dcl.init.list] Example 13 (paragraph 6), docs/cxx-spec/dcl/dcl.init.list.md
// needs: P5
// status: annotated

typedef std::complex<double> cmplx;
std::vector<cmplx> v1 = { 1, 2, 3 };

void f() {
  std::vector<cmplx> v2{ 1, 2, 3 };
  std::initializer_list<int> i3 = { 1, 2, 3 };
}

struct A {
  std::initializer_list<int> i4;
  A() : i4{ 1, 2, 3 } {}            // ill-formed, would create a dangling reference // semantic
};
