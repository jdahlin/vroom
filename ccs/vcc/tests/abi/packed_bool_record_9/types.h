typedef _Bool B __attribute__((ext_vector_type(9)));
struct S{B b;};
struct S f(struct S);
#define LANES 9
