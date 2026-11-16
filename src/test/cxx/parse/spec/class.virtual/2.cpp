// [class.virtual] Example 2 (paragraph 2), docs/cxx-spec/class/class.virtual.md
// needs: P5
// status: well-formed (as far as the example says)

struct A { virtual void f(); };
struct B : A { };
struct C : A { void f(); };
struct D : B, C { };            // OK: A::f and C::f are the final overriders
                                // for the B and C subobjects, respectively
