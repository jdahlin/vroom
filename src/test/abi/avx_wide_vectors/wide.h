/* vectors wider than the widest register (AVX): arguments in memory, results
   in two or four ymm registers, then through the hidden pointer */
typedef int v16si __attribute__((vector_size(64)));
typedef double v16df __attribute__((vector_size(128)));
typedef long long v32di __attribute__((vector_size(256)));
typedef __int128 v2ti __attribute__((vector_size(32)));
struct box {
    v16si v;
};

v16si add(v16si a, int by, v16si b);
v16df twice(v16df a);
v32di thrice(v32di a);
v2ti swapped(v2ti a);
struct box boxed(struct box x);
int variadic(int n, ...);
v16si named_variadic(v16si first, int n, ...);
v16si callback(v16si (*fn)(v16si, int), v16si x);
v16si times(v16si x, int by);
