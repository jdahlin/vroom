// [class.prop] Example 1 (paragraph 4), docs/cxx-spec/class/class.prop.md
// needs: P5
// status: well-formed (as far as the example says)

struct B { int i; };            // standard-layout class
struct C : B { };               // standard-layout class
struct D : C { };               // standard-layout class
struct E : D { char : 4; };     // not a standard-layout class

struct Q {};
struct S : Q { };
struct T : Q { };
struct U : S, T { };            // not a standard-layout class
