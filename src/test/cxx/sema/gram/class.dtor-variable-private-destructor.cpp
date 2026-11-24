// [class.dtor]/16, [class.access]/1: the destructor of a variable's class is
// potentially invoked from the variable's definition: a private one is
// inaccessible outside the class and its friends.
class X { ~X(); };
X x;
void f() { X y; }
class Fr { ~Fr(); friend void g(); };
void g() { Fr z; }
