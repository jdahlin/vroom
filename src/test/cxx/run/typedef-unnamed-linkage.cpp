// a typedef names an unnamed class or enumeration for linkage: foo(E) is _Z3foo1E (llvm-test-suite 2003-08-20-EnumSizeProblem)
typedef enum { A = 0, B = 5 } E;
typedef struct { int a; } P;
namespace n { typedef enum { C = 7 } F; }
int foo(E x) { return x; }
int bar(P p) { return p.a; }
int baz(n::F f) { return f; }
int main() {
	P p = { 3 };
	return foo(B) + bar(p) + baz(n::C) - 15;
}
