/* vcc-flags: -O2 */
/* a switch with several cases to a block with a phi, and another case to a block
 * that only sets the phi's value and branches there (sqlite3_db_status's
 * CACHE_SPILL falling into CACHE_HIT/MISS/WRITE): under VIR the edge split must
 * route cases 8 and 9 through their own edge block, not through case 12's */
__attribute__((noinline)) int g(int x) { return x; }

__attribute__((noinline)) int f(int op)
{
	int r;

	switch (op) {
	case 0: r = g(100); break;
	case 1: r = g(101); break;
	case 2: r = g(102); break;
	case 3: r = g(103); break;
	case 4: case 5: case 6: r = g(op + 200); break;
	case 12: op = 10; /* fall through */
	case 7: case 8: case 9: r = g(op); break;
	case 10: r = g(110); break;
	case 11: r = g(111); break;
	default: r = -1;
	}
	return r;
}

int main(void)
{
	static const int want[14] = { 100, 101, 102, 103, 204, 205, 206, 7, 8, 9, 110, 111, 10, -1 };

	for (int i = 0; i < 14; i++)
		if (f(i) != want[i])
			return 1 + i;
	return 0;
}
