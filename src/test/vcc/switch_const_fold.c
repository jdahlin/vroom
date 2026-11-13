/* switch on a constant (the kernel's switch (sizeof(x)) in put_user etc.): only the selected case is generated, the others' calls (bad(), never defined) are gone, as clang -O2 */
extern void bad(void);
#define PUT(x) ({ switch (sizeof(x)) { case 1: g = 1; break; case 2: g = 2; break; case 4: g = 4; break; default: bad(); } })
int g;
void f(void) { int y = 0; char z = 0; PUT(y); PUT(z); switch (3) { case 1 ... 5: g += 10; default: g += 100; } }
int main(void) { f(); return g != 111; }
