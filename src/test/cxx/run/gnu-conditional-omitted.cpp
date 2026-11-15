// GNU's a ?: b: a evaluated once; of two lvalues an lvalue (llvm-test-suite conditional-gnu-ext-cxx; its _Complex part: not yet)
static int calls, glob = 1;
int g() { ++calls; return 5; }
int z() { ++calls; return 0; }
int &lv() { ++calls; return glob; }
void inc(int &x) { ++x; }
int main() {
	int r = 10;
	int a = g() ?: r, b = z() ?: r;
	inc(lv() ?: r);
	return a == 5 && b == 10 && calls == 3 && glob == 2 ? 0 : 1;
}
