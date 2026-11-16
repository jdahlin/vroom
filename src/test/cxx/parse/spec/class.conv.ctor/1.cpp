// [class.conv.ctor] Example 1 (paragraph 1), docs/cxx-spec/class/class.conv.ctor.md
// needs: P5
// status: well-formed (as far as the example says)

struct X {
    X(int);
    X(const char*, int =0);
    X(int, int);
};

void f(X arg) {
  X a = 1;          // a = X(1)
  X b = "Jessie";   // b = X("Jessie",0)
  a = 2;            // a = X(2)
  f(3);             // f(X(3))
  f({1, 2});        // f(X(1,2))
}
