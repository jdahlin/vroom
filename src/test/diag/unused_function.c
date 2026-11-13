/* diag-flags: -Wall */
static void a(void) {}
static int b(void);
static inline void c(void) {}
static void d(void) {}
static int e = 1;
static void g(void) __attribute__((unused));
static void g(void) {}
static void h(void);
static void h(void) {}
void k(void) { d(); }
static void n(void) { }
static void (*fp)(void) = n;
__attribute__((used)) static void o(void) {}
static void q(void) {}
static void r(void) { q(); }
static int self = sizeof(self);
