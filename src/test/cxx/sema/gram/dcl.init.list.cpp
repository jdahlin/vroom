// List-initialization of a class by constructors ([dcl.init.list]/3.7,
// [over.match.list]/1.2): a constructor whose first parameter is a class
// other than std::initializer_list is an ordinary candidate (std::nullopt_t's
// shape: two tag arguments).
struct tag {
  explicit tag() = default;
};
struct pair_of_tags {
  int made;
  explicit pair_of_tags(tag, tag) : made(2) {}
};
struct wrapper {
  int made;
  wrapper(pair_of_tags q, int k) : made(q.made + k) {}
};
int direct() {
  pair_of_tags a{tag{}, tag{}};
  wrapper w{pair_of_tags{tag{}, tag{}}, 5};
  return a.made + w.made;
}
