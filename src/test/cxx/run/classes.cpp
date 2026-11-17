// classes without constructors: [class.mem] layout, [expr.ref] member
// access, [class.mfct] member functions in and outside the class,
// [class.this], [class.access], [dcl.init.aggr], the trivial copy
// ([class.copy.ctor], [class.copy.assign]), classes by value
struct Point {
    int x, y;
    int sum() const { return x + y; }
    void move(int dx, int dy);
    Point scaled(int k) const;
    static int origin_distance(Point p) { return p.x * p.x + p.y * p.y; }
};

void Point::move(int dx, int dy) {
    x += dx;
    this->y += dy;
}

Point Point::scaled(int k) const {
    Point p = {x * k, y * k};
    return p;
}

struct Mixed {
    char c;
    long l;
    short s;
    double d;
    char tail;
};

struct Rect {
    Point corner, size;
    int area() const { return size.x * size.y; }
};

struct Node;
struct Node {
    int value;
    Node *next;
    int length() const { return next ? 1 + next->length() : 1; }
};

class Counter {
    int count;
    int step;

  public:
    void reset(int s) {
        count = 0;
        step = s;
    }
    void tick() { count += step; }
    int value() const { return count; }
    int twice() { return times(2); }

  private:
    int times(int n) { return count * n; }
};

struct Empty {};

Point origin = {};
Point unit = {1};
Rect global_rect = {{1, 2}, {3, 4}};

int by_value(Point p) {
    p.x = 100;
    return p.x + p.y;
}

Point make(int a, int b) {
    Point p = {a, b};
    return p;
}

int main() {
    int total = 0;
    Point p = {3, 4};
    total += p.sum();
    p.move(1, 2);
    total += p.x * 10 + p.y;
    Point q = p;
    q.x = 0;
    total += p.x - q.x;
    q = make(5, 6);
    total += q.sum() + by_value(q) + q.x;
    Point *pp = &q;
    pp->y = 9;
    total += pp->sum() + (*pp).x;
    total += Point::origin_distance(p.scaled(2));
    total += origin.x + origin.y + unit.x + unit.y;
    total += global_rect.area() + global_rect.corner.y;
    Rect r = {p, {2, 3}};
    r.size.y += 1;
    total += r.area() + r.corner.x;
    Node c = {3, nullptr}, b = {2, &c}, a = {1, &b};
    total += a.length() * 100 + a.next->next->value;
    Counter counter;
    counter.reset(3);
    counter.tick();
    counter.tick();
    total += counter.value() + counter.twice();
    Point points[3] = {{1, 1}, {2, 2}};
    for (int i = 0; i < 3; i++)
        total += points[i].sum();
    total += sizeof(Point) + sizeof(Mixed) * 10 + sizeof(Empty) + sizeof(Rect);
    const Point k = {7, 8};
    total += k.sum() + k.y;
    return total & 255;
}
