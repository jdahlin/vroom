/* "expected ';' after struct" (clang's isValidAfterTypeSpecifier): at the end of
   the '}' (or of "struct B" ending its line), and parsing goes on as if it were there */
struct A { int x; }
int a;
union U { int i; float f; }
static int b;
enum E { E0, E1 }
typedef int T;
struct B
int c;
/* valid: these may follow the body */
struct C { int y; } const cc1 = { 1 }, *cp;
struct D { int z; } __attribute__((aligned(8))) d;
struct F { struct G { int w; } g; } f;
int use(void) { return a + b + c + E1 + (int)sizeof(T) + cc1.y + d.z + f.g.w + (int)sizeof(struct { int q; }); }
