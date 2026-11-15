// a variable length array in a body (GNU's extension): C's VLA type, its bound evaluated once (llvm-test-suite DynAllocAndRestore)
struct M { int n; int get() const { return n; } };
int sum(const M &m) {
	int k = m.get(), t[k], s = 0;
	for (int i = 0; i < k; ++i)
		t[i] = i;
	for (int i = 0; i < k; ++i)
		s += t[i];
	return s + (int)(sizeof t / sizeof t[0]);
}
int main() { M m = { 5 }; int calls = 0; for (int r = 0; r < 3; ++r) calls += sum(m); return calls == 3 * (10 + 5) ? 0 : 1; }
