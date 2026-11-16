// [class.member.lookup] Example 5 (paragraph 11), docs/cxx-spec/class/class.member.lookup.md
// needs: P5
// status: annotated

struct V { };
struct A { };
struct B : A, virtual V { };
struct C : A, virtual V { };
struct D : B, C { };

void g() {
  D d;
  B* pb = &d;
  A* pa = &d;       // error: ambiguous: C's A or B's A? // semantic
  V* pv = &d;       // OK: only one V subobject
}
