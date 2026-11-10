/* D1.0: field and bit-field messages name the field and its type */
struct X;
struct A { int f(void); };
struct B { struct X x; };
struct C { float b : 3; };
struct D { int c : -1; };
struct E { int d : 0; };
