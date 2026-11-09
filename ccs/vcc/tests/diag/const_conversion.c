/* D5.1: -Wconstant-conversion and the bit-field variants, clang's ranges (an
   unsigned target keeps the bits of -2^(w-1) .. 2^w-1, a byte in braces takes
   -128 .. 255), each with the range the target holds */
unsigned char uc = 300;
short sh = 70000;
signed char sc = 128;
signed char table[2] = { 128, 255 };
unsigned u = -1;
int big = 4294967295L;
#define LIMIT 400
char from_macro = LIMIT;
char pick(int v) { return v ? 999 : 1; }
void call(short s);
void use(void) { call(65536); }
struct flags { int mode : 2; unsigned kind : 3; int on : 1; };
struct flags f = { 2, 8, 1 };
void set(struct flags *p) { p->kind = 9; p->mode = -3; p->on = 0; }
