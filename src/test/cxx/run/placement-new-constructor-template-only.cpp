// [expr.new]/23, [dcl.init]/17.6.2: a new-expression's parenthesized expression-list for a class
// whose only constructors are templates (no implicit copy constructor: a base's is deleted) calls
// the constructor overload resolution chooses (libc++'s __shared_ptr_emplace in make_shared)
typedef unsigned long size_t;
void *operator new(size_t, void *p) noexcept { return p; }
struct base {
  base() {}
  base(const base &) = delete;
  virtual ~base() {}
};
template <class T> struct cb : base {
  int a;
  template <class... X> explicit cb(T, X... x) : a(sizeof...(X)) {}
};
int main() {
  alignas(cb<int>) char buf[sizeof(cb<int>)];
  cb<int> *p = ::new ((void *)buf) cb<int>(1, 2, 3);
  int a = p->a;
  p->~cb();
  return a == 2 ? 0 : 1;
}
