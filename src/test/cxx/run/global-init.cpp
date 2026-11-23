// [dcl.link] extern "C", and [basic.start.dynamic] dynamic initialization
// of namespace-scope variables, in the order of their definitions,
// before main
extern "C" {
int counter;
int bump(int by) { return counter += by; }
}
extern "C" int twice(int x) { return 2 * x; }

struct Point {
    int x, y;
    Point(int a, int b) : x(a), y(b) { bump(1); }
    int sum() const { return x + y; }
};

int f() { return bump(10); }

int first = f();
Point p(3, 4);
int second = first + p.sum();
Point q = Point(twice(5), 1);
int &alias = second;
const int folded = 6;

int main() {
    alias += folded;
    return (counter * 7 + first + second + q.sum() + p.x) & 255;
}
