/* D5.5: more -pedantic extensions, as clang 23 (gnu11): typeof/asm keywords, extra ';',
   binary literals, empty initializers, enum range and forward references, void arithmetic */
/* diag-flags: -std=gnu11 -pedantic */
#define V(a...) a
#define W(...) __VA_ARGS__
int c1 = __COUNTER__;
typeof(c1) t1;
int a1 __asm__("a1x");
int a2 asm("a2x");
;
struct X { int a;; };
int b = 0b101;
int arr[2] = {};
struct Y { int y; } yy = {};
enum E { BIG = 0x80000000 };
enum F;
int s = sizeof(void) + _Alignof(void);
void *vp;
void *q(void) { return vp + 1; }
long d(void *p) { return p - vp; }
void vf(void);
void vg(void) { return vf(); }
int f2(void) { asm("nop"); __asm__("nop"); return V(1) + W(2); }
