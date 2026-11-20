// RUN: %clang_cc1 -std=c++20 %s

// [basic.lookup.qual]/1: the name before :: is looked up ignoring variables
// and functions, so a parameter named like the alias does not hide it.
template <class K> struct HasBase { using base = int; static constexpr int count = 3; };
template <class K> struct Shadowing {
  using set = HasBase<K>;
  static int f(const set& set) { return typename set::base(); }
  static int g(const set& set) { return set::count; }
  static int h(const set& set) { typename set::base b = 0; return b; }
};
struct Holder { using type = int; };
void shadowed(int Holder) { typename Holder::type x = Holder; (void)x; }
