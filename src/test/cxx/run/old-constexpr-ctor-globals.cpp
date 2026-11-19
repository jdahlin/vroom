// globals and inline static members built by a constructor with an empty body and constant
// mem-initializers (libc++'s strong_ordering::less{_OrdResult::__less}): constant data, read by address;
// a defaulted operator== on a prvalue ((b <=> a) == o)
enum class R : signed char { lt = -1, eq = 0, gt = 1 };
struct O {
  signed char v;
  int k;
  constexpr explicit O(R r) noexcept : v((signed char)r), k((int)r * 10 + 3) {}
  friend constexpr bool operator==(O, O) noexcept = default;
  static const O less, greater;
};
inline constexpr O O::less(R::lt);
inline constexpr O O::greater{R::gt};
constexpr O g3(R::eq);
const O *pick(int i) { return i < 0 ? &O::less : i > 0 ? &O::greater : &g3; }
O make(int i) { return O(i < 0 ? R::lt : i > 0 ? R::gt : R::eq); }
int main() {
  return pick(-1)->k == -7 && pick(1)->v == 1 && pick(0)->k == 3 && make(5) == O::greater && !(make(-5) == *pick(0)) ? 0 : 1;
}
