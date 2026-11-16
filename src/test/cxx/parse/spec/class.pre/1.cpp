// [class.pre] Example 1 (paragraph 5), docs/cxx-spec/class/class.pre.md
// needs: P5
// status: well-formed (as far as the example says)

struct A;
struct A final {};      // OK: definition of struct A,
                        // not value-initialization of variable final

struct X {
 struct C { constexpr operator int() { return 5; } };
 struct B final : C{};  // OK: definition of nested class B,
                        // not declaration of a bit-field member final
};
