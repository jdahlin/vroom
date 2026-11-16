// [class.abstract] Example 1 (paragraph 2), docs/cxx-spec/class/class.abstract.md
// needs: P5
// status: well-formed (as far as the example says)

class point { /* ... */ };
class shape {                   // abstract class
  point center;
public:
  point where() { return center; }
  void move(point p) { center=p; draw(); }
  virtual void rotate(int) = 0; // pure virtual
  virtual void draw() = 0;      // pure virtual
};
