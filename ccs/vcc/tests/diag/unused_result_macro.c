/* -Wunused-result: a statement expression from a macro does not warn for its dropped value (clang; the kernel's drmm_mutex_init), one written out does */
int __attribute__((warn_unused_result)) f(int);
#define M(x) ({ do { } while (0); f(x); })
void g(void) { M(1); }
void h(void) { ({ do { } while (0); f(2); }); }
