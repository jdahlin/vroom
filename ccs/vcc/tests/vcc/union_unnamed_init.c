/* Unnamed bit-fields consume no positional initializer, including in unions. */
union scalar { unsigned : 0; unsigned : 3; int value; long alternate; };
union complex_value { char : 0; _Complex char value; };
union nested { int : 0; struct { int x, y; }; long alternate; };
struct wrapped { int before; union scalar value; int after; };

static union scalar gs = { 37 };
static union complex_value gc = { 3 + 4i };
static union nested gn = { { 5, 6 } };
static struct wrapped gw = { 7, 8, 9 };

int main(void)
{
	volatile int n = 41;
	union scalar s = { n };
	union complex_value c = { 11 + 12i };
	union nested a = { { n, 13 } };
	union nested b = { 14, 15 };
	union scalar d = { .alternate = 17 };
	struct wrapped w = { 18, n, 19 };

	if (gs.value != 37 || s.value != 41 || d.alternate != 17) return 1;
	if (__real gc.value != 3 || __imag gc.value != 4) return 2;
	if (__real c.value != 11 || __imag c.value != 12) return 3;
	if (gn.x != 5 || gn.y != 6 || a.x != 41 || a.y != 13) return 4;
	if (b.x != 14 || b.y != 15) return 5;
	if (gw.before != 7 || gw.value.value != 8 || gw.after != 9) return 6;
	if (w.before != 18 || w.value.value != 41 || w.after != 19) return 7;
	return 0;
}
