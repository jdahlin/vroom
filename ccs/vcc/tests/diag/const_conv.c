/* diag-flags: -Wno-literal-conversion -Wno-bitfield-constant-conversion -Wno-unused-value */
char a = 300;
unsigned char b = 256;
signed char c = 128;
short s = 70000;
unsigned char ok1 = 255;
signed char ok2 = -128;
char ok3 = 255;
unsigned char m1 = -1;
struct { int f : 3; unsigned g : 2; } bf;
void f(void) {
	char x = 1000;
	x = 512;
	bf.f = 8;
	bf.g = 5;
	bf.f = 4;
	int i = 3000000000;
	unsigned u = -1;
	long l = 1;
	x = 1 ? 300 : 0;
	(void)x; (void)i; (void)u; (void)l;
	float fl = 1e40;
	int fi = 1e20;
	int fi2 = 1.5;
	(void)fl; (void)fi; (void)fi2;
}
void g(char p);
void h(void) { g(1000); }
char r(void) { return 999; }
void g2(short);
#define HB (0x8d)
void f2(int a) {
	g2((0, 35536)); g2(35536);
	short sa = (35536), sb = -(-35536);
	signed char h1 = 0x80, h2 = a ? 0xc0 : HB;
	unsigned short m = (1 ? 5 : 100000);
	(void)sa; (void)sb; (void)h1; (void)h2; (void)m;
}
