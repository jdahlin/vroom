// C++: ++x and --x are lvalues: x after the update, bound to a reference parameter (libc++'s
// move_backward: __iter_move(--__last_iter)), through a member, and assigned to
template <class I> int &&mv(I &&i) { return static_cast<int &&>(*i); }
static int &ref(int *&p) { return *p; }
static int &iref(int &i) { return i; }
struct S { int *p; int k; };
int f1(int *e) { int *p = e + 2; int &&r = mv(--p); return r + (p == e + 1 ? 0 : 100); }
int f2(int *e) { int *p = e + 2; return ref(--p); }
int f3(int *e) { int *p = e; return ref(++p); }
int f4(S *s) { return ref(++s->p) + (iref(--s->k) == 4 ? 0 : 100); }
int g; int f5() { iref(++g) += 10; return g; }
int main() { int a[3] = {1, 2, 3}; S s{a, 5}; return f1(a) == 2 && f2(a) == 2 && f3(a) == 2 && f4(&s) == 2 && s.k == 4 && f5() == 11 ? 0 : 1; }
