// std: c++20
extern "C" int printf(const char*, ...);
namespace std {
struct partial_ordering {
  signed char __value_;
  static const partial_ordering less, equivalent, greater, unordered;
  friend constexpr bool operator<(partial_ordering a, decltype(nullptr)) { return a.__value_ == -1; }
  friend constexpr bool operator>(partial_ordering a, decltype(nullptr)) { return a.__value_ == 1; }
};
inline constexpr partial_ordering partial_ordering::less{-1}, partial_ordering::equivalent{0}, partial_ordering::greater{1}, partial_ordering::unordered{-127};
struct strong_ordering {
  signed char __value_;
  static const strong_ordering less, equal, greater;
  constexpr operator partial_ordering() const noexcept { return partial_ordering{__value_}; }
  friend constexpr bool operator<(strong_ordering a, decltype(nullptr)) { return a.__value_ < 0; }
  friend constexpr bool operator>(strong_ordering a, decltype(nullptr)) { return a.__value_ > 0; }
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::greater{1};
constexpr bool is_lt(partial_ordering p) noexcept { return p.__value_ == -1; }
}
/* json's value_t: operator<=> through an order table, operator< from it */
enum class V : unsigned char { a, b, c };
inline std::partial_ordering operator<=>(const V l, const V r) noexcept {
  static constexpr unsigned char order[3] = {2, 0, 1};
  unsigned li = static_cast<unsigned>(l), ri = static_cast<unsigned>(r);
  if (li < 3 && ri < 3)
    return order[li] <=> order[ri];	/* strong_ordering to partial_ordering */
  return std::partial_ordering::unordered; }
inline bool lt(const V l, const V r) { return std::is_lt(l <=> r); }
enum E { x, y };
inline bool operator<(E, E) { return false; }	/* a user operator on enum constants: called, not folded */
int main() { printf("%d %d %d %d %d\n", lt(V::a, V::b), lt(V::b, V::a), (int)(V::a < V::b), (int)(x < y), std::is_lt(1 <=> 2)); }
