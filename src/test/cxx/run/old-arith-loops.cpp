int add(int a, int b) { return a + b; }
int fact(int n) { int r = 1; while (n > 1) { r = r * n; n = n - 1; } return r; }
long sum_to(long n) { long s = 0; long i; i = 1; while (i <= n) { s += i; i = i + 1; } return s; }
int main() {
	if (fact(5) != 120) return 1;
	if (add(2, 3) != 5) return 2;
	if (sum_to(100) != 5050) return 3;
	return 0;
}
