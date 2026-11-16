// [class.name] Example 4 (paragraph 2), docs/cxx-spec/class/class.name.md
// needs: P5
// status: well-formed (as far as the example says)

class Vector;

class Matrix {
  // ...
  friend Vector operator*(const Matrix&, const Vector&);
};

class Vector {
  // ...
  friend Vector operator*(const Matrix&, const Vector&);
};
