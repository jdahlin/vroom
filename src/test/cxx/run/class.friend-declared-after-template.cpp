/* [temp.friend]: a specialization whose friend class is declared after the
   template, here by an earlier friend declaration too
   (SkTBlockList and TBlockListTestAccess). */
class A { friend class Acc; int a = 1; };
template <class T> class L { friend class Acc; int b = 2; public: L() {} };
class Acc { public: static int get() { A a; L<int> l; return a.a + l.b; } };
int main(){ return Acc::get() - 3; }
