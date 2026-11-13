typedef _Bool B __attribute__((ext_vector_type(2)));
struct S{B b;};
struct S f(struct S);
#define LANES 2
