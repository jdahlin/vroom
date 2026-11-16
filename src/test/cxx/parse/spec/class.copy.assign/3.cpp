// [class.copy.assign] Example 3 (paragraph 12), docs/cxx-spec/class/class.copy.assign.md
// needs: P5
// status: well-formed (as far as the example says)

struct V { };
struct A : virtual V { };
struct B : virtual V { };
struct C : B, A { };
