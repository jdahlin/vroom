// [class.abstract] Example 3, part 2 (paragraph 4), docs/cxx-spec/class/class.abstract.md
// needs: P5
// status: well-formed (as far as the example says)

class circle : public shape {
  int radius;
public:
  void rotate(int) { }
  void draw();                  // a definition is required somewhere
};
