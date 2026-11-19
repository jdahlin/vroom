/* if/else chains, while with break and continue, logical operators, recursion, overloads */
int fib(int n) { if (n < 2) return n; return fib(n - 1) + fib(n - 2); }
int collatz(long n) { int steps = 0; while (n != 1) { if (n % 2 == 0) n = n / 2; else n = 3 * n + 1; steps += 1; } return steps; }
int first_multiple(int of, int above) { int i = above; while (true) { i += 1; if (i % of != 0) continue; break; } return i; }
bool between(int x, int lo, int hi) { return lo <= x && x <= hi; }
bool outside(int x, int lo, int hi) { return x < lo || x > hi; }
int pick(int x) { return 1; }
int pick(long x) { return 2; }
int pick(double x) { return 3; }
double half(double x) { return x / 2; }
unsigned mask(unsigned x, int bits) { return x & ((1u << bits) - 1); }
int sign(int x) { if (x > 0) { return 1; } else if (x < 0) { return 0 - 1; } else { return 0; } }
int main() {
	if (fib(15) != 610) return 1;
	if (collatz(27) != 111) return 2;
	if (first_multiple(7, 50) != 56) return 3;
	if (!between(5, 1, 9)) return 4;
	if (between(10, 1, 9)) return 5;
	if (!outside(10, 1, 9)) return 6;
	if (pick(1) != 1) return 7;
	if (pick(1L) != 2) return 8;
	if (pick(1.5) != 3) return 9;
	if (half(5) != 2.5) return 10;
	if (mask(0xabcdu, 8) != 0xcdu) return 11;
	if (sign(5) + sign(0 - 7) + sign(0) != 0) return 12;
	return 0;
}
