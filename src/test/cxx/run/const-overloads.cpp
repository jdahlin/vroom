// std: c++20
// f() and f() const are two functions (the same function type in vcc's AST): a non-const object calls
// f(), a const one f() const
struct A {
  int g() { return 1; }
  int g() const { return 2; }
  int h() const { return 4; }
};
int main() {
  A a;
  const A ca{};
  const A &r = a;
  return (a.g() == 1 && ca.g() == 2 && r.g() == 2 && a.h() == 4) ? 0 : 1;
}
