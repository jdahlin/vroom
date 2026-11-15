// function-scope statics of a class with a constructor or destructor: constructed when the declaration first
// runs (guarded: __cxa_guard_acquire/release), destroyed at exit (__cxa_atexit), in every initializer form
extern "C" int printf(const char *, ...);
int made, gone;
struct S {
	int v;
	S() : v(1) { made++; }
	S(int a, int b) : v(a + b) { made++; }
	S(const S &o) : v(o.v) { made++; }
	~S() { gone++; }
};
S mk(int x) { return S(x, 0); }
int a() { static S s; return s.v++; }
int b() { static S s(2, 3); return s.v++; }
int c() { static S s{4, 5}; return s.v++; }
int d() { static S s = mk(7); return s.v++; }
int e() { static S s = S(1, 1); return s.v++; }
struct Report { ~Report() { if (gone != 5) printf("destroyed %d\n", gone); } } report;
int main()
{
	int r = 0;
	for (int i = 0; i < 3; i++)
		r += a() + b() + c() + d() + e();
	/* 87: (1+2+3) + (5+6+7) + (9+10+11) + (7+8+9) + (2+3+4) */
	return r == 87 && made >= 5 && made <= 7 && gone <= 2 ? 0 : 1;
}
