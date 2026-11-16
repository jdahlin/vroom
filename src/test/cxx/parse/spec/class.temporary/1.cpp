// [class.temporary] Example 1 (paragraph 2), docs/cxx-spec/basic/class.temporary.md
// needs: P5
// status: well-formed (as far as the example says)

class X {
public:
  X(int);
  X(const X&);
  X& operator=(const X&);
  ~X();
};

class Y {
public:
  Y(int);
  Y(Y&&);
  ~Y();
};

X f(X);
Y g(Y);

void h() {
  X a(1);
  X b = f(X(2));
  Y c = g(Y(3));
  a = f(a);
}
