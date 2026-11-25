// [stmt.return]/2, [class.temporary]/4: a return statement whose operand is a braced-init-list
// of a class's members, each initialized from a call that makes a class temporary (libc++'s
// __copy_move_unwrap_iters: return {std::__rewrap_range<_Sent>(...), std::__rewrap_iter(...)}):
// the temporaries are made before the result and destroyed after it
static int destroyed;
struct tmp {
  int v;
  ~tmp() { destroyed++; }
};
static int value_of(const tmp &t) { return t.v; }
struct pair {
  int a, b;
};
static pair make(int x) { return {value_of(tmp{x}), value_of(tmp{x + 1})}; }
int main() {
  pair p = make(3);
  return p.a == 3 && p.b == 4 && destroyed == 2 ? 0 : 1;
}
