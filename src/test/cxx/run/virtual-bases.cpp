/* [class.mi]/4, [abi:class-types] III: a diamond of virtual bases; the
   virtual tables' vbase offsets, the VTT and the construction virtual
   tables ([abi:vtable-ctor-vtt]), conversions to the virtual base through
   the vbase offset, of a pointer with side effects too */
extern "C" int printf(const char *, ...);
struct A { int a; A() : a(1) {} virtual int fa() { return a; } };
struct B : virtual A { int b; B() : b(2) {} virtual int fb() { return b + a; } };
struct C : virtual A { int c; C() : c(3) {} virtual int fc() { return c * 10 + a; } };
struct D : B, C { int d; D() : d(4) { a = 7; } };
int use(D *p) { return p->fb() + p->fc() + p->fa(); }
int viaA(A *p) { return p->fa(); }
int viaC(C *p) { return p->fc() + p->a; }
int calls;
D *counted(D *p) { calls++; return p; }
int main() {
  D d; B b; C c;
  printf("%d %d %d %d\n", use(&d), viaA(&d), viaC(&d), d.a);
  printf("%d %d %d\n", b.fb(), viaA(&b), viaC(&c));
  A *pa = &d; C *pc = &d;
  printf("%d %d\n", pa->fa(), pc->fc());
  printf("%d %d\n", viaA(counted(&d)), calls);
  return 0;
}
