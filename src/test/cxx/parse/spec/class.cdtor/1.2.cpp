// [class.cdtor] Example 1, part 2 (paragraph 1), docs/cxx-spec/class/class.cdtor.md
// needs: P5
// status: well-formed (as far as the example says)

struct W { int j; };
struct X : public virtual W { };
struct Y {
  int* p;
  X x;
  Y() : p(&x.j) {   // undefined, x is not yet constructed
    }
};
