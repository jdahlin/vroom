// std: c++20
// an empty [[no_unique_address]] member copied in a constructor's initializer writes nothing: its
// address is another member's (std::__tree's __value_comp_ at __size_), which it must not clobber
struct Cmp { bool operator()(int a, int b) const { return a < b; } };
struct Tree {
  long size;
  [[no_unique_address]] Cmp comp;
  explicit Tree(const Cmp &c) : size(0), comp(c) {}
};
int main() {
  unsigned char junk[8] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  const Cmp &c = *reinterpret_cast<const Cmp *>(junk);
  Tree t(c);
  return t.size == 0 && sizeof(Tree) == sizeof(long) && t.comp(1, 2) ? 0 : 1;
}
