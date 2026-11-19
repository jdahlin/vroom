// a = {...} of a class: the list is a T prvalue for operator=(T) (json's by-value operator=), or for the
// implicit one; before, the list's last element was passed (r["c"] = {{"a", 1}, {"b", 2}} stored 2)
struct P { int a, b; P() : a(0), b(0) {} P(int x, int y) : a(x), b(y) {} P &operator=(P o) { a = o.a * 10; b = o.b * 10; return *this; } };
struct Q { int a, b; };
struct R { int v; R() : v(0) {} R(int x, int y) : v(x + y) {} };
int main() {
	P p; p = {1, 2};
	Q q{0, 0}; q = {3, 4};
	R r; r = {5, 6};
	int s = 0; s = {7};
	return p.a == 10 && p.b == 20 && q.a == 3 && q.b == 4 && r.v == 11 && s == 7 ? 0 : 1;
}
