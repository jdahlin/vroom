// [special] Example 1 (paragraph 2), docs/cxx-spec/class/special.md
// needs: P5
// status: well-formed (as far as the example says)

struct A { };                   // implicitly declared A::operator=
struct B : A {
  B& operator=(const B &);
};
B& B::operator=(const B& s) {
  this->A::operator=(s);        // well-formed
  return *this;
}
