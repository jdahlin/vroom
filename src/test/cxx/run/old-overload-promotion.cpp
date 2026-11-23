// a promotion ranks better than other standard conversions: abs(short) is abs(int) (libc++'s <cstdlib>)
int a(int) { return 1; }
int a(long) { return 2; }
int a(double) { return 3; }
int b(long) { return 1; }
int b(double) { return 2; }
enum E { X };
int main() {
	short s = 0;
	unsigned char uc = 0;
	bool t = true;
	float f = 0;
	return a(s) == 1 && a(uc) == 1 && a(t) == 1 && a(X) == 1 && b(f) == 2 && a(f) == 3 ? 0 : 1;
}
