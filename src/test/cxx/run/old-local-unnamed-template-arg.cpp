// a local unnamed class as a template argument (libc++'s __scalar_hash: std::addressof of a local union)
template <class T> T *addr(T &x) { return __builtin_addressof(x); }
unsigned long h(double v) {
	union { double t; unsigned long a; } u;
	u.t = v;
	return addr(u)->a;
}
unsigned long k(double v) {
	struct { double t; } u;
	u.t = v;
	return *(unsigned long *)addr(u);
}
int main() { return h(1.0) == k(1.0) ? 0 : 1; }
