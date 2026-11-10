/* records of integers by value across compilers: every size from 1 to 24
   bytes, mixed members, bit-fields, results, register exhaustion, varargs */
#include <stdarg.h>
struct s1 { char a; };
struct s3 { char a, b, c; };
struct s4 { short a; char b; };
struct s7 { char a[7]; };
struct s8 { int a, b; };
struct s12 { int a, b, c; };
struct s16 { long a, b; };
struct s16m { char a; long b; };
struct s24 { long a, b, c; };
struct sbf { unsigned a : 3, b : 12; int c : 9; };
struct sptr { char *p; int n; };

struct s1 r1(int);
struct s3 r3(int);
struct s7 r7(int);
struct s12 r12(int);
struct s16 r16(long);
struct s16m r16m(long);
struct s24 r24(long);
struct sbf rbf(int);
long sum_all(struct s1, struct s3, struct s4, struct s7, struct s8, struct s12, struct s16, struct s16m, struct s24, struct sbf);
long spill(long a, long b, long c, long d, long e, struct s16 x, struct s12 y, long f, struct s8 z);
long vsum(int n, ...);	/* n pairs: a tag (0: long, 1: struct s16, 2: struct s24, 3: struct s3) and the value */
long back(long (*f)(struct s16, struct s3, long), long);	/* calls f, a function of the caller */
