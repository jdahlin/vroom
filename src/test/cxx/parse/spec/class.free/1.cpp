// [class.free] Example 1 (paragraph 2), docs/cxx-spec/class/class.free.md
// needs: P7 (std::size_t is not declared)
// status: annotated

class Arena;
struct B {
  void* operator new(std::size_t, Arena*);
};
struct D1 : B {
};

Arena*  ap;
void foo(int i) {
  new (ap) D1;      // calls B::operator new(std::size_t, Arena*)
  new D1[i];        // calls ::operator new[](std::size_t)
  new D1;           // error: ::operator new(std::size_t) hidden // semantic
}
