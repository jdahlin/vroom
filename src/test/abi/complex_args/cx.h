/* _Complex across compilers: float (one SSE word on x86-64), double (two),
   long double (x86-64: memory in, st(0)/st(1) out), int; by value, returned,
   in records and through varargs */
_Complex float cf(_Complex float a, _Complex float b, float k);
_Complex double cd(_Complex double a, int i, _Complex double b);
_Complex long double cld(long double a, _Complex long double z, _Complex float f, _Complex double d);
_Complex int ci(_Complex int a, _Complex short s);
struct rc { _Complex float f; double d; };
struct rc crec(struct rc r, _Complex double z);
double cvar(int n, ...);
