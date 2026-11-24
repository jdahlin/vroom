// [dcl.mptr]/2 and [basic.lookup.qual]/1: the class of a pointer to member
// may be incomplete; only its name is looked up
struct U;
using P = int U::*;
using Q = void (U::*)();
struct V { struct W; };
int V::W::* r;
static_assert(sizeof(P) == sizeof(long));
