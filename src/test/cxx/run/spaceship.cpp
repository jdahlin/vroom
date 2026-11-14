// std: c++20
// operator<=> declared and called (as an operator and by name), and a < b, <=, >, >= rewritten as
// (a <=> b) @ 0 or, reversed, 0 @ (b <=> a) [over.match.oper]
namespace std {	// (as libc++'s: a signed char -1, 0, 1)
struct strong_ordering {
  signed char __value_;
  friend constexpr bool operator<(strong_ordering v, int) { return v.__value_ < 0; }
  friend constexpr bool operator>(strong_ordering v, int) { return v.__value_ > 0; }
  friend constexpr bool operator<=(strong_ordering v, int) { return v.__value_ <= 0; }
  friend constexpr bool operator>=(strong_ordering v, int) { return v.__value_ >= 0; }
  friend constexpr bool operator<(int, strong_ordering v) { return 0 < v.__value_; }
  friend constexpr bool operator>(int, strong_ordering v) { return 0 > v.__value_; }
  friend constexpr bool operator<=(int, strong_ordering v) { return 0 <= v.__value_; }
  friend constexpr bool operator>=(int, strong_ordering v) { return 0 >= v.__value_; }
};
}
struct A { int v; };
std::strong_ordering operator<=>(const A &a, int b) { return {(signed char)(a.v < b ? -1 : a.v > b ? 1 : 0)}; }
int main() {
  A one{1}, three{3};
  int r = (one < 2) + 2 * (three >= 2) + 4 * (2 < three) + 8 * (three <= 3) + 16 * (one > 1) + 32 * (operator<=>(one, 1).__value_ == 0);
  return r == 1 + 2 + 4 + 8 + 32 ? 0 : 1;
}
