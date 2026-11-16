// [expr.prim.lambda.capture] Example 6 (paragraph 8), docs/cxx-spec/expr/expr.prim.lambda.capture.md
// needs: P6
// status: annotated

void f1(int i) {
  int const N = 20;
  auto m1 = [=]{
    int const M = 30;
    auto m2 = [i]{
      int x[N][M];              // OK: N and M are not odr-used
      x[0][0] = i;              // OK: i is explicitly captured by m2 and implicitly captured by m1
    };
  };
  struct s1 {
    int f;
    void work(int n) {
      int m = n*n;
      int j = 40;
      auto m3 = [this,m] {
        auto m4 = [&,j] {       // error: j not odr-usable due to intervening lambda m3 // semantic
          int x = n;            // error: n is odr-used but not odr-usable due to intervening lambda m3 // semantic
          x += m;               // OK: m implicitly captured by m4 and explicitly captured by m3
          x += i;               // error: i is odr-used but not odr-usable // semantic
                                // due to intervening function and class scopes
          x += f;               // OK: this captured implicitly by m4 and explicitly by m3
        };
      };
    }
  };
}

struct s2 {
  double ohseven = .007;
  auto f() {
    return [this] {
      return [*this] {
          return ohseven;       // OK
      };
    }();
  }
  auto g() {
    return [] {
      return [*this] { };       // error: *this not captured by outer lambda-expression // semantic
    }();
  }
};
