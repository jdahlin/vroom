// [temp.expl.spec]/14: the explicit specialization of a static data member
// without an initializer is a declaration, not a definition: no
// default-initialization, no second definition.
struct NoDefault {
  NoDefault(int);
};
template <typename T> struct X {
  static T value;
};
constexpr unsigned size = sizeof(X<NoDefault>) + sizeof(X<int>);
template <> NoDefault X<NoDefault>::value;
NoDefault &use() { return X<NoDefault>::value; }
