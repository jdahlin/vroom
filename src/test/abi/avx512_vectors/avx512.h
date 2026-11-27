/* 512-bit vectors by value (System V with AVX-512): zmm registers, then the
   stack; records and unions of one; variadic ones in memory; results of two
   or four zmm registers */
typedef int v16si __attribute__((vector_size(64)));
typedef float v16sf __attribute__((vector_size(64)));
typedef char v64qi __attribute__((vector_size(64)));
typedef double v8df __attribute__((vector_size(64)));
typedef int v8si __attribute__((vector_size(32)));
typedef long long v2di __attribute__((vector_size(16)));
typedef int v32si __attribute__((vector_size(128)));
typedef int v64si __attribute__((vector_size(256)));
struct box {
    v16si v;
};
struct arrayed {
    v16si v[1];
};
struct halves {
    v8si a, b;
};
union overlaid {
    v16si v;
    v8si half;
};

v16si add(v16si a, v16si b);
v16sf mix(int i, v16sf a, double d, v8df b, long n, v64qi c);
v16si many(v16si a, v16si b, v16si c, v16si d, v16si e, v16si f, v16si g,
           v16si h, int i, v16si j, v2di k, v8si m, v16si l);
struct box boxed(struct box x, int by);
struct arrayed arrayed(struct arrayed x);
struct halves halves(struct halves x);
union overlaid overlaid(union overlaid x);
v32si twice(v32si a);
v64si thrice(v64si a);
int variadic(int n, ...);
v16si named_variadic(v16si first, int n, ...);
v16si callback(v16si (*fn)(v16si, int), v16si x);
v16si times(v16si x, int by);
__attribute__((target("avx512bw"))) v64qi bytes(v64qi a, v64qi b);
