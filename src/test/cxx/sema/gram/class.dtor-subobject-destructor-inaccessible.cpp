// [class.dtor]/16, [class.access]: a user-provided destructor invokes the
// destructors of its bases and members; an inaccessible one is ill-formed.  (An out-of-class definition: clang reports the class, we the definition.)
class X { ~X(); };
class Y : X { ~Y() {} };
struct M { X x; ~M() {} };
class Friendly { ~Friendly(); friend struct Holder; };
struct Holder { Friendly f; ~Holder() {} };
class Protected { protected: ~Protected(); };
struct Derived : Protected { ~Derived() {} };
