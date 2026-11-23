// [class.name] Example 1, part 4 (paragraph 1), docs/cxx-spec/class/class.name.md
// needs: P5
// status: annotated

struct S { int a; };
struct S { int a; };            // error: double definition // semantic
