// [class.free] Example 2 (paragraph 5), docs/cxx-spec/class/class.free.md
// needs: P7 (std::size_t is not declared)
// status: well-formed (as far as the example says)

class X {
  void operator delete(void*);
  void operator delete[](void*, std::size_t);
};

class Y {
  void operator delete(void*, std::size_t);
  void operator delete[](void*);
};
