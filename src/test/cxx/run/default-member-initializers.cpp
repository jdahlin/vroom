/* [class.mem]/10, [class.base.init]/9.1: default member initializers,
   used by the implicit default constructor and by a constructor whose
   mem-initializer-list leaves the member out */
struct Point {
    int x = 3;
    int y{4};
    long scaled = x * 10L;
};

struct Counter {
    int start = 7;
    int step = 2;
    Counter() {}
    Counter(int s) : start(s) {}
    int next() const { return start + step; }
};

struct Holder {
    Point p;
    int tag = 1;
};

int main()
{
    Point p;
    Counter a, b(10);
    Holder h;
    int bad = 0;
    if (p.x != 3 || p.y != 4 || p.scaled != 30) bad |= 1;
    if (a.next() != 9 || b.next() != 12) bad |= 2;
    if (h.p.y != 4 || h.tag != 1) bad |= 4;
    return bad;
}
