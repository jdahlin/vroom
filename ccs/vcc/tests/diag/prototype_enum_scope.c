/* diag-flags: -Wno-visibility */
enum { A = 1 };
char (*f(enum { A = 2, B,C,D,E,F,G,H,I,J,K,L,M,N,O,P,Q,R,S,T } *p,
         int (*a)[A], double (*b)[sizeof **a]))[A] {
    _Static_assert(A == 2, "parameter scope");
    _Static_assert(T == 21, "all enumerators retained");
    _Static_assert(sizeof *b == sizeof(int) * sizeof(double), "parameter bound");
    _Static_assert(sizeof *a == 2 * sizeof(int), "inner bound");
    return 0;
}
_Static_assert(A == 1, "file scope");
_Static_assert(sizeof *f(0, 0, 0) == 1, "return scope");
