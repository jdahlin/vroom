// [class.copy.ctor] Example 2 (paragraph 2), docs/cxx-spec/class/class.copy.ctor.md
// needs: P5
// status: well-formed (as far as the example says)

struct Y {
  Y(const Y&);
  Y(Y&&);
};
extern Y f(int);
Y d(f(1));          // calls Y(Y&&)
Y e = d;            // calls Y(const Y&)
