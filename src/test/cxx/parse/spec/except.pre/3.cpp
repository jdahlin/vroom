// [except.pre] Example 3 (paragraph 4), docs/cxx-spec/except/except.pre.md
// needs: P6
// status: well-formed (as far as the example says)

int f(int);
class C {
  int i;
  double d;
public:
  C(int, double);
};

C::C(int ii, double id)
try : i(f(ii)), d(id) {
    // constructor statements
} catch (...) {
    // handles exceptions thrown from the ctor-initializer and from the constructor statements
}
