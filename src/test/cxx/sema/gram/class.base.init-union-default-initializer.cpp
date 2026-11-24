// [class.base.init]/9.1-9.2: a variant member's default member initializer is used only when the constructor
// designates no other variant member; the mem-initializer's member is the active one (cwg1455-style union B).
union B {
  int a, b = 2, c;
  constexpr B() : a(1) {}
  constexpr B(char) : b(4) {}
  constexpr B(int) : c(3) {}
};
static_assert(B().a == 1, "");
static_assert(B('x').b == 4, "");
static_assert(B(0).c == 3, "");
