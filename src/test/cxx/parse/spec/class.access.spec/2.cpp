// [class.access.spec] Example 2 (paragraph 2), docs/cxx-spec/class/class.access.spec.md
// needs: P5
// status: well-formed (as far as the example says)

struct S {
  int a;            // S::a is public by default: struct used
protected:
  int b;            // S::b is protected
private:
  int c;            // S::c is private
public:
  int d;            // S::d is public
};
