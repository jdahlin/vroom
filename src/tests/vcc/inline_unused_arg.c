/* an inlined function's unused argument without side effects is not evaluated (the kernel's __acquire_shared_ctx_lock(RCU): RCU is never defined) */
struct ctx;
extern const struct ctx *RCU;
static inline __attribute__((always_inline)) void acq(const struct ctx *v) { }
int g;
void f(void) { acq(RCU); g = 1; }
int main(void) { f(); return g != 1; }
