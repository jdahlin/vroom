// [class.ctor] constructors, [class.base.init] member initializers,
// [class.dtor] destructors run at the end of their scope, on return,
// break and continue ([stmt.dcl]/2), in reverse order of construction
int trace[64];
int traced;

void note(int v) { trace[traced++] = v; }

struct Point {
    int x, y;
    Point() : x(0), y(0) {}
    Point(int v) : x(v), y(v) {}
    Point(int a, int b) : y(b), x(a) {}
    int sum() const { return x + y; }
};

struct Guard {
    int id;
    Guard(int i);
    ~Guard();
};

Guard::Guard(int i) : id(i) { note(id); }
Guard::~Guard() { note(-id); }

struct Inline {
    int id;
    Inline(int i) : id(i) { note(100 + id); }
    ~Inline() { note(-100 - id); }
};

int scoped(int n) {
    Guard a(1);
    if (n > 0) {
        Guard b(2);
        if (n > 1)
            return n * 10;
    }
    Inline c(3);
    return n;
}

int loops() {
    int total = 0;
    for (int i = 0; i < 4; i++) {
        Guard g(10 + i);
        if (i == 1)
            continue;
        if (i == 3)
            break;
        total += g.id;
    }
    int k = 0;
    while (k < 2) {
        Inline in(k);
        k++;
    }
    switch (k) {
    case 2: {
        Guard s(50);
        break;
    }
    default:
        break;
    }
    return total;
}

Point twice(Point p) { return Point(p.x * 2, p.y * 2); }

int main() {
    int total = 0;
    Point a;
    Point b(3);
    Point c(4, 5);
    Point d = 7;
    Point e{};
    Point f = Point(1, 2);
    total += a.sum() + b.sum() + c.sum() * 2 + d.sum() + e.sum() + f.sum();
    total += twice(c).sum();
    Point g = c;
    total += g.x;
    total += scoped(0) + scoped(1) + scoped(2);
    total += loops();
    {
        Guard x(7), y(8);
        Inline z(9);
    }
    int check = 0;
    for (int i = 0; i < traced; i++)
        check = check * 31 + trace[i];
    return (total + check + traced) & 255;
}
