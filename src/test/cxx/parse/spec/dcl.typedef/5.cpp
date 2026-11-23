// [dcl.typedef] Example 5 (paragraph 5), docs/cxx-spec/dcl/dcl.typedef.md
// needs: P5
// status: well-formed (as far as the example says)

struct S;
typedef struct S S;
int main() {
  struct S* p;                  // OK
}
struct S { };                   // OK
