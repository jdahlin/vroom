// [class.union.anon]/1, /3: a static anonymous union at namespace scope declares an unnamed variable of
// internal linkage; its members are bound in the namespace; a static data member is ill-formed there and in a class.
namespace n {
static union {
  int i;
  float f;
};
}
template <int &> struct S {};
static union {
  int j;
};
S<j> v;
