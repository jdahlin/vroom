// [dcl.init.aggr]/8: the destructor of each element of class type is
// potentially invoked from the context of the aggregate initialization.
class X { int f; ~X(); };
struct Y { X x; };
void inaccessible() { auto *y = new Y {}; }
class Friend { ~Friend(); friend void allowed(); };
struct Z { Friend f; };
void allowed() { auto *z = new Z {}; }
