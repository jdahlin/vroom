// a class reference parameter bound to an object of another class: a temporary made by the parameter
// class's converting constructor (libc++'s unique_ptr<node, __tree_deleter>(p, alloc), D&& and const D&)
struct Alloc { int n; };
struct D { Alloc &a; D(Alloc &al) : a(al) {} void operator()(int *p) { a.n += *p; } };
struct U {
  int *p; D d;
  U(int *q, D &&e) : p(q), d(e) {}
  ~U() { d(p); }
};
int take(const D &d) { return d.a.n; }
int main() {
  Alloc al{1};
  int v = 5;
  { U u(&v, al); }
  return al.n == 6 && take(al) == 6 ? 0 : 1;
}
