// goto and labels (json's lexer: goto scan_number_minus;), backward and forward, out of a switch, to a
// label before a compound statement.
int count() { int i = 0; again: if (++i < 3) goto again; return i; }
int pick(int c)
{
	int r = 0;
	switch (c) {
	case 1: r = 1; goto done;
	case 2: r = 2; goto done;
	default: break;
	}
	r = 9;
done:
	return r;
}
int twice(int c) { if (c == 0) goto zero; goto any; zero: return 0; any: { int k = c * 2; return k; } }
int main() { return (count() != 3) | (pick(1) != 1) << 1 | (pick(2) != 2) << 2 | (pick(3) != 9) << 3 | (twice(0) != 0 || twice(3) != 6) << 4; }
