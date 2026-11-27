// [dcl.stc]: register until C++17 ([diff.cpp14.dcl.dcl]/1), one declaration
// per rule: bench/cxx-sema.py compares each verdict with clang's.
void block(register int a) { register int b = a; (void)b; }
void with_static() { static register int x; }   // error: two storage classes
void with_extern() { extern register int y; }   // error
void with_typedef() { typedef register int I; } // error
register int at_namespace;                      // error
register void function();                        // error
struct S { register int member; };              // error
void local_function() { register void g(); }    // error
