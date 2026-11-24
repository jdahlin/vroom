// [expr.ass]/9, [over.match.oper]: x = {} with both copy and move assignment.
struct A {
    int x = 3, y = 4;
    int moved = 0;
    A() = default;
    A(int a, int b) : x(a), y(b) {}
    A &operator=(const A &o) { x = o.x; y = o.y; return *this; }
    A &operator=(A &&o) { x = o.x; y = o.y; moved = 1; return *this; }
    void reset() { *this = {}; }
};
int main()
{
    A a(7, 8);
    a.reset();
    if (a.x != 3 || a.y != 4 || a.moved != 1)
        return 1;
    a = {5, 6};
    if (a.x != 5 || a.y != 6)
        return 2;
    return 0;
}
