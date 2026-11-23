// [class.member.lookup] Example 4 (paragraph 10), docs/cxx-spec/class/class.member.lookup.md
// needs: P5
// status: well-formed (as far as the example says)

struct V { int f();  int x; };
struct W { int g();  int y; };
struct B : virtual V, W {
  int f();  int x;
  int g();  int y;
};
struct C : virtual V, W { };

struct D : B, C { void glorp(); };
