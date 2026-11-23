// [over.sub] Example 1 (paragraph 2), docs/cxx-spec/over/over.sub.md
// needs: P5
// status: annotated

struct X {
  Z operator[](std::initializer_list<int>);
};
X x;
x[{1,2,3}] = 7;                 // OK: meaning x.operator[]({1,2,3})
int a[10];
a[{1,2,3}] = 7;                 // error: built-in subscript operator // syntax-error
