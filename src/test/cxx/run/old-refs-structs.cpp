/* references, plain structs, member functions */
void inc(int &x) { x = x + 1; }
int twice(const int &x) { return x * 2; }
int &pick(int &a, int &b, bool first) { if (first) return a; return b; }
void swap_(long &a, long &b) { long t = a; a = b; b = t; }
int through_ref(int v) { int &r = v; r += 5; const int &cr = r; return cr + v; }
struct Point { int x; int y; };
struct Rect { Point lo; Point hi; long tag; };
int area(const Rect &r) { return (r.hi.x - r.lo.x) * (r.hi.y - r.lo.y); }
Point mid(Point a, Point b) { Point m; m.x = (a.x + b.x) / 2; m.y = (a.y + b.y) / 2; return m; }
void move(Point *p, int dx, int dy) { p->x += dx; p->y += dy; }
void grow(Rect &r, int d) { r.lo.x -= d; r.lo.y -= d; r.hi.x += d; r.hi.y += d; r.tag++; }
struct Counter {
	int n;
	int step;
	void reset(int s) { n = 0; step = s; }
	void tick() { n += step; }
	int value() const { return n; }
	int scaled(int k) const { return value() * k; }
	Counter &twice() { tick(); tick(); return *this; }
	static int limit() { return 100; }
};
struct Vec { double x, y; double dot(const Vec &o) const; Vec plus(Vec o) const; };
double Vec::dot(const Vec &o) const { return x * o.x + y * o.y; }
Vec Vec::plus(Vec o) const { Vec r; r.x = x + o.x; r.y = y + o.y; return r; }
namespace geo { struct Box { int w, h; int area() const { return w * h; } }; int total(const Box &a, const Box &b) { return a.area() + b.area(); } }
int main() {
	int a = 1; inc(a); inc(a); if (a != 3) return 1;
	if (twice(a) != 6 || twice(7) != 14) return 2;
	int b = 10; pick(a, b, false) = 20; pick(a, b, true) += 1; if (a != 4 || b != 20) return 3;
	long p = 1, q = 2; swap_(p, q); if (p != 2 || q != 1) return 4;
	if (through_ref(1) != 12) return 5;
	Rect r; r.lo.x = 1; r.lo.y = 2; r.hi.x = 5; r.hi.y = 8; r.tag = 0;
	if (area(r) != 24) return 6;
	Point m = mid(r.lo, r.hi); if (m.x != 3 || m.y != 5) return 7;
	move(&m, 2, -1); if (m.x != 5 || m.y != 4) return 8;
	grow(r, 1); if (area(r) != 48 || r.tag != 1) return 9;
	Counter c; c.reset(3); c.tick(); c.tick(); if (c.value() != 6) return 10;
	if (c.scaled(2) != 12) return 11;
	if (c.twice().value() != 12) return 12;
	if (Counter::limit() != 100) return 13;
	Vec u; u.x = 1; u.y = 2; Vec v; v.x = 3; v.y = 4;
	if (u.dot(v) != 11) return 14;
	Vec w = u.plus(v); if (w.x != 4 || w.y != 6) return 15;
	geo::Box b1; b1.w = 2; b1.h = 3; geo::Box b2; b2.w = 4; b2.h = 5; if (geo::total(b1, b2) != 26) return 16;
	Rect copy = r; copy.tag = 9; if (r.tag != 1 || copy.lo.x != r.lo.x) return 17;
	return 0;
}
