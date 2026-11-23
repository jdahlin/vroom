// [class.mem] Example 5 (paragraph 25), docs/cxx-spec/class/class.mem.md
// needs: P5
// status: well-formed (as far as the example says)

struct T1 { int a, b; };
struct T2 { int c; double d; };
union U { T1 t1; T2 t2; };
int f() {
  U u = { { 1, 2 } };   // active member is t1
  return u.t2.c;        // OK, as if u.t1.a were nominated
}
