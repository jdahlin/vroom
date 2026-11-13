typedef float f4 __attribute__((ext_vector_type(4)));
typedef int i4 __attribute__((ext_vector_type(4)));
typedef int v4 __attribute__((vector_size(16)));
f4 a(void) { f4 h = 1; return h; }
f4 b(f4 x) { return x * 2; }
f4 c(f4 x) { x = 3.0f; return x; }
v4 d(v4 v) { return v + 1; }
