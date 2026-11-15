// inline constexpr static members defined outside their class (libc++'s strong_ordering::less) are weak
// with their values; a const variable at the global scope has internal linkage; both readable
struct O {
  signed char v;
  static const O less, greater;
};
inline constexpr O O::less{-1};
inline constexpr O O::greater{1};
struct P { int k; };
inline constexpr P pp{7};
constexpr P qq{8};
int use(const O *a, const O *b, const P *c, const P *d) { return a->v == -1 && b->v == 1 && c->k == 7 && d->k == 8 ? 0 : 1; }
int main() { return use(&O::less, &O::greater, &pp, &qq); }
