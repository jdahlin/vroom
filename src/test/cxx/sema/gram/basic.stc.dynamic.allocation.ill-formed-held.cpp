// [basic.stc.dynamic.allocation]/1, [over.oper]/1: an allocation function declared ill-formed with no prior entity holds the name `operator new`, not the token `operator`: a later declaration of an operator is not confused with it
typedef decltype(sizeof(0)) size_t;
namespace NS {
  void *operator new(size_t);
}
struct A { void *operator new(size_t); };
struct B { };
bool operator==(B, B);
static void *operator new(size_t, int, int);
bool f(B a, B b) { return a == b; }
