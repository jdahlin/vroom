/* ifunc: an STT_GNU_IFUNC alias of its resolver; calls and the address through it
   (clang makes a static ifunc global too) */
extern int pick;
static int impl1(void) { return 1; }
static int impl2(void) { return 2; }
static int (*resolve(void))(void) { return pick ? impl1 : impl2; }
int f(void) __attribute__((ifunc("resolve")));
static int sf(void) __attribute__((ifunc("resolve")));
int g(void) { return f() + sf(); }
int (*h(void))(void) { return f; }
