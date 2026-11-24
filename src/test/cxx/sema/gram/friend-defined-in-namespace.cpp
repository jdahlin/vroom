// [class.friend]/6, [namespace.memdef]/3: a function declared by a friend
// declaration is defined at namespace scope beside other overloads of its
// name (clang's std-compare.h).

namespace n {
class B;
struct A {
  friend constexpr bool operator==(A, int) noexcept;
  friend constexpr bool operator==(int, A) noexcept;
  int v;
};
constexpr bool operator==(A, A) noexcept { return true; }
constexpr bool operator==(A a, int i) noexcept { return a.v == i; }
constexpr bool operator==(int i, A a) noexcept { return a.v == i; }
static_assert(A{1} == 1, "");
static_assert(2 == A{2}, "");
}
