// [class.friend] Example 9 (paragraph 11), docs/cxx-spec/class/class.friend.md
// needs: P5
// status: annotated

class X;
void a();
void f() {
  class Y;
  extern void b();
  class A {
  friend class X;   // OK, but X is a local class, not ::X
  friend class Y;   // OK
  friend class Z;   // OK, introduces local class Z
  friend void a();  // error, ::a is not considered // semantic
  friend void b();  // OK
  friend void c();  // error // semantic
  };
  X* px;            // OK, but ::X is found
  Z* pz;            // error: no Z is found // semantic
}
