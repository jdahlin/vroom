/* members of anonymous structs and unions, [[no_unique_address]] */
struct E { };
struct R0 { int a; long b; };
struct Plain { struct { R0 rep_; int x; }; long get() const { return rep_.b; } int gx() const { return x; } };
long h(const Plain &s) { return s.rep_.b + s.x; }
struct Num { union { int i; float f; }; int kind; int as_int() const { return i; } };
template <class C> struct Str {
	struct L { unsigned long cap; unsigned long size; C *data; };
	struct S { unsigned char size; C data[23]; };
	union R { S s; L l; };
	struct { [[no_unique_address]] R rep_; [[no_unique_address]] E alloc_; };
	C *long_ptr() { return rep_.l.data; }
	unsigned long long_size() const { return rep_.l.size; }
	void set(C *p, unsigned long n) { rep_.l.data = p; rep_.l.size = n; }
};
int main() {
	Plain s; s.rep_.b = 4; s.x = 1; if (s.get() != 4 || s.gx() != 1 || h(s) != 5) return 1;
	Num n; n.i = 7; n.kind = 0; if (n.as_int() != 7) return 2;
	char buf[4]; Str<char> t; t.set(buf, 3); if (t.long_ptr() != buf || t.long_size() != 3) return 3;
	if (sizeof(Str<char>) != 24) return 4;
	return 0;
}
