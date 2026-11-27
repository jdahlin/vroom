/* [expr.cast]/4: a C-style cast to a reference to const of a class no
   implicit conversion reaches is a reinterpret_cast */
struct Point {
    double x, y;
};
struct Cubic {
    Point pts[4];
};
struct Pair {
    Point pts[7];
    Cubic first() const { return (const Cubic &)pts[0]; }
    const Cubic &second() const { return (const Cubic &)pts[3]; }
};
int main()
{
    Pair p{};
    for (int i = 0; i < 7; i++)
        p.pts[i].x = i;
    if (p.first().pts[2].x != 2 || p.second().pts[3].x != 6)
        return 1;
    return &p.second().pts[0] != &p.pts[3];
}
