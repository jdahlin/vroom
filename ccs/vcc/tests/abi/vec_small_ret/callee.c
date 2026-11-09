typedef char v4qi __attribute__((vector_size(4)));
typedef short v2hi __attribute__((vector_size(4)));
typedef float v1sf __attribute__((vector_size(4)));
typedef signed char v2qi __attribute__((vector_size(2)));
typedef short v1hi __attribute__((vector_size(2)));
typedef char v1qi __attribute__((vector_size(1)));

v4qi r4(int k) { return (v4qi){ (char)k, 2, 3, 4 }; }
v2hi rh(int k) { return (v2hi){ (short)k, -7 }; }
v1sf rf(float x) { return (v1sf){ x * 2 }; }
v2qi r2(int k) { return (v2qi){ (signed char)k, -3 }; }
v1hi r1h(int k) { return (v1hi){ (short)k }; }
v1qi r1q(int k) { return (v1qi){ (char)k }; }
