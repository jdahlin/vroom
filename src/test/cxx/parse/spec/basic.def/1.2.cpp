// [basic.def] Example 1, part 2 (paragraph 2), docs/cxx-spec/basic/basic.def.md
// needs: (the step whose grammar parses it)
// status: well-formed (as far as the example says)

extern int a;                   // declares a
extern const int c;             // declares c
int f(int);                     // declares f
struct S;                       // declares S
typedef int Int;                // declares Int
extern X anotherX;              // declares anotherX
using N::d;                     // declares d
