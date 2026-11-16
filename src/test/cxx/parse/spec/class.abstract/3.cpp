// [class.abstract] Example 3 (paragraph 4), docs/cxx-spec/class/class.abstract.md
// needs: P5
// status: well-formed (as far as the example says)

class ab_circle : public shape {
  int radius;
public:
  void rotate(int) { }
  // ab_circle::draw() is a pure virtual
};
