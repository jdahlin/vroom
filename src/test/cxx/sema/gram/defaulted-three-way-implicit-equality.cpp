// [class.compare.default]/5, [dcl.fct.def.default]/3: the == a defaulted <=> declares is constexpr as it is; a
// constexpr <=> whose members' == is not constexpr is ill-formed
namespace std {
struct strong_ordering {
  signed char v;
  static const strong_ordering less, equal, equivalent, greater;
};
inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, strong_ordering::equivalent{0}, strong_ordering::greater{1};
constexpr bool operator!=(strong_ordering v, int) { return v.v != 0; }
}
struct H {
  bool operator==(const H&) const;
  constexpr std::strong_ordering operator<=>(const H&) const { return std::strong_ordering::equal; }
};
struct I {
  H h;
  constexpr std::strong_ordering operator<=>(const I&) const = default; // expected-error {{constexpr}}
};
struct J {
  H h;
  std::strong_ordering operator<=>(const J&) const = default;
};
struct K {
  int i;
  constexpr std::strong_ordering operator<=>(const K&) const = default;
};
