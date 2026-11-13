/* short vectors (8/16 bytes) go in v registers, alone or in HVAs (AAPCS64) */
#include <stdarg.h>

typedef int v4si __attribute__((vector_size(16)));
typedef unsigned char v8qi __attribute__((vector_size(8)));
typedef float v4sf __attribute__((vector_size(16)));
typedef double v2df __attribute__((vector_size(16)));
typedef short v4hi __attribute__((vector_size(8)));
typedef int v8si __attribute__((vector_size(32)));	/* not short: by reference */
typedef char v4qi __attribute__((vector_size(4)));	/* 4 bytes: a GPR */

struct hva2 { v4si a, b; };
struct hva3 { v8qi a, b, c; };
struct hva4 { v2df a[2]; v2df b, c; };
struct mix { v4si a; double d; };		/* not homogeneous: by reference */
struct hfa2 { float x, y; };

v4si add4(v4si a, v4si b);
v8qi mix8(int i, v8qi a, double d, v4hi h);
v4sf fma4(v4sf a, v4sf b, v4sf c, struct hfa2 s);
v2df many(v2df a, v2df b, v2df c, v2df d, v2df e, v2df f, v2df g, v2df h, v2df i, long tail);
struct hva2 swap2(struct hva2 h);
struct hva3 rot3(struct hva3 h);
struct hva4 sum4(struct hva4 h, struct hva4 k);
struct mix mixed(struct mix m, v8si big);
v8si big8(v8si a);
int small4(v4qi a, v4qi b);
long vsum(int n, ...);			/* n v4si after n */
v4si call_back(v4si (*f)(v4si, v8qi), v4si a);
