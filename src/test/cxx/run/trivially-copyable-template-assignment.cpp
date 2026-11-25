// [class.copy.assign]/1: a member function template operator= is no copy or move assignment
// operator, so a class with one keeps its implicit, trivial ones and is trivially copyable
// (libc++'s move_iterator, __is_trivially_copyable in pair's trivially relocatable test)
template <class It> struct mi {
  It i;
  mi() = default;
  template <class U> mi &operator=(const mi<U> &o) { i = o.i; return *this; }
};
struct user {
  int x;
  user &operator=(const user &o) { x = o.x + 1; return *this; }
  template <class U> user &operator=(const U &) { return *this; }
};
static_assert(__is_trivially_copyable(mi<char *>), "");
static_assert(!__is_trivially_copyable(user), "");
int main() {
  mi<char *> b;
  char c = 0;
  b.i = &c;
  mi<char *> a(b);
  return a.i == &c ? 0 : 1;
}
