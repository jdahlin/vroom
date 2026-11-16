// [class.member.lookup] Example 3 (paragraph 9), docs/cxx-spec/class/class.member.lookup.md
// needs: P5
// status: annotated

struct V {
  int v;
};
struct A {
  int a;
  static int   s;
  enum { e };
};
struct B : A, virtual V { };
struct C : A, virtual V { };
struct D : B, C { };

void f(D* pd) {
  pd->v++;          // OK: only one v (virtual)
  pd->s++;          // OK: only one s (static)
  int i = pd->e;    // OK: only one e (enumerator)
  pd->a++;          // error: ambiguous: two as in D // semantic
}
