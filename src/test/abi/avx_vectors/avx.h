/* 256-bit vectors by value (System V with AVX): ymm registers, then the
   stack; records of one; variadic ones in memory */
typedef int v8si __attribute__((vector_size(32)));
typedef float v8sf __attribute__((vector_size(32)));
typedef double v4df __attribute__((vector_size(32)));
typedef char v32qi __attribute__((vector_size(32)));
typedef long long v2di __attribute__((vector_size(16)));
struct box {
    v8si v;
};
struct pair {
    v8si a, b;
};

/* unions and structs clang also passes in a ymm register */
union overlaid {
    v8si v;
    long a[4];
};
struct padded {
    v8si v;
    unsigned : 0;
};
union overlaid overlaid(union overlaid x);
struct padded padded(struct padded x);

v8si add(v8si a, v8si b);
v8sf mix(int i, v8sf a, double d, v4df b, long n, v32qi c);
v8si many(v8si a, v8si b, v8si c, v8si d, v8si e, v8si f, v8si g, v8si h,
          int i, v8si j, v2di k, v8si l);
struct box boxed(struct box x, int by);
struct pair paired(struct pair x);
int variadic(int n, ...);
v8si named_variadic(v8si first, int n, ...);
v8si callback(v8si (*fn)(v8si, int), v8si x);
v8si times(v8si x, int by);
