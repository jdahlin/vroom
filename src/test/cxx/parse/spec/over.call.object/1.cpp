// [over.call.object] Example 1 (paragraph 3), docs/cxx-spec/over/over.call.object.md
// needs: P5
// status: well-formed (as far as the example says)

int f1(int);
int f2(float);
typedef int (*fp1)(int);
typedef int (*fp2)(float);
struct A {
  operator fp1() { return f1; }
  operator fp2() { return f2; }
} a;
int i = a(1);                   // calls f1 via pointer returned from conversion function
