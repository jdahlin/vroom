// [class.base.init]/9.2: a user-provided constructor leaves an anonymous union's variant members it
// does not name uninitialized; the union's deleted default constructor is not called (libc++'s
// __tree_node: union { __node_value_type __value_; }, constructed in place); [class.dtor]/13: the
// destructor destroys no variant member (no destructor of the unnamed union is called)
typedef unsigned long size_t;
void *operator new(size_t, void *p) noexcept { return p; }
struct str { int n; str() : n(1) {} str(int k) : n(k) {} ~str() { n = 0; } };
template <class V> struct node {
  int color;
  union { V value; };
  template <class... A> explicit node(int c, A... a) : color(c) { ::new ((void *)&value) V(a...); }
  ~node() {}
};
int main() { node<str> n(2, 7); int v = n.value.n; n.value.~str(); return n.color == 2 && v == 7 ? 0 : 1; }
