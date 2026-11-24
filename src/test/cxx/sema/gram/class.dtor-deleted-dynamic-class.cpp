// [class.dtor]/6: a dynamic class whose implicit destructor is deleted by a
// subobject: its virtual table slots are __cxa_deleted_virtual.
struct A { ~A() = delete; };
struct P { virtual ~P() = delete; virtual int g() { return 3; } };
struct B : P { virtual int f(); A a; };
