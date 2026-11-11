/* diag-differs: vcc prints the underlying typedef spelling. */
typedef _Atomic int A;
_Atomic(A) nested;
_Atomic(const int) qualified;
void f(_Atomic int *p) { int *q = p; (void)q; }
