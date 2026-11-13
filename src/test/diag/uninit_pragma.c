/* D8.5: -Wuninitialized follows #pragma clang diagnostic (the flag state is asked per
   function then, not once per TU) */
/* diag-flags: -Wno-uninitialized */
int off(void) { int x; return x; }
#pragma clang diagnostic push
#pragma clang diagnostic warning "-Wuninitialized"
int on(void) { int y; return y; }
#pragma clang diagnostic pop
int off2(int c) { int z; if (c) z = 1; return z; }
#pragma clang diagnostic warning "-Wsometimes-uninitialized"
int on2(int c) { int v; if (c) v = 1; return v; }
