// Names that are ill-formed, one declaration per rule, with near misses
// that are well-formed ([namespace.udir]/6, [namespace.qual], [dcl.meaning],
// [namespace.memdef], [namespace.def], [namespace.alias], [namespace.udecl],
// [basic.link]/6, [dcl.stc], [dcl.init]): bench/cxx-sema.py compares each
// verdict with clang's.
namespace P { int amb = 1; int only_p = 2; typedef int same; struct tag {}; }
namespace Q { int amb = 3; int only_q = 4; typedef int same; }
namespace R { using namespace P; using namespace Q; }
using namespace P;
using namespace Q;
int u1 = amb;
int u2 = only_p + only_q;
same u3 = 0;
int u4 = R::amb;
int u5 = R::only_p;
namespace M { int member; void fn(); }
namespace Other {
int M::member;
void M::fn() {}
}
int M::member;
void M::fn() {}
namespace Named { }
inline namespace Named { }
int Named;
namespace Al = P;
namespace Al = Q;
namespace Al2 = P;
namespace Al2 = P;
int var1;
namespace var1 { }
struct C1 { int cm; static int sm; };
using C1::sm;
using P::tag;
using P;
void blocks() {
  static void sf();
  extern int ei = 1;
  int local;
  extern int local;
  extern int fine;
  void fine2();
}
int qualifiedClass = C1::missing;
int qualifiedStatic = C1::sm;
struct Derived : C1 { using M::member; using C1::cm; };
