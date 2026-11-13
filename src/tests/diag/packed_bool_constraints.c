/* diag-differs: logical operands are diagnosed separately; vector component and cast diagnostics point at their operators. */
typedef _Bool B2 __attribute__((ext_vector_type(2)));
typedef _Bool B3 __attribute__((ext_vector_type(3)));
B2 add(B2 a, B2 b) { return a + b; }
B2 shift(B2 a, B2 b) { return a << b; }
B2 logical(B2 a, B2 b) { return a && b; }
B2 float_bits(B2 a) { return a & 1.0; }
B2 swizzle(B2 a) { return a.xy; }
_Bool *address(B2 *a) { return &(*a)[1]; }
B3 resize(B2 a) { return (B3)a; }
