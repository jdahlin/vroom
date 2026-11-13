typedef int v4 __attribute__((vector_size(16)));
typedef float f4 __attribute__((ext_vector_type(4)));
struct S { int a; };
void f(v4 a, f4 b, struct S s) { int *p = a; float *q = b; v4 c = s; }
