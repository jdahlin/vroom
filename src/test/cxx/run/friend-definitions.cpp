// [class.friend]/6-7: functions defined in friend declarations (Skia's
// SkPoint, SkM44 operators): members of the enclosing namespace, inline,
// found by argument-dependent lookup only, their bodies in the class's
// scope with access to its private members
namespace geo {
struct P {
    float x, y;
    friend bool operator==(const P& a, const P& b) { return a.x == b.x && a.y == b.y; }
    friend bool operator!=(const P& a, const P& b) { return !(a == b); }
    friend P operator-(const P& a, const P& b) { return {a.x - b.x, a.y - b.y}; }
    friend P operator*(const P& a, float s) { return {a.x * s, a.y * s}; }
    friend constexpr float dot(const P& a, const P& b) { return a.x * b.x + a.y * b.y; }
};
class Counter {
    int n = 0;
    static constexpr int step = 2;
    friend void bump(Counter& c) { c.n += step; }
    friend int value(const Counter& c) { return c.n; }
public:
    Counter() = default;
};
}  // namespace geo
int main()
{
    geo::P p{1, 2}, q{1, 2}, r{3, 4};
    geo::Counter c;
    /* (a call statement of a name only argument-dependent lookup finds,
       bump(c);, the parser does not tell from a declaration yet) */
    int bumped = (bump(c), bump(c), 0);
    return p == q && p != r && (r - p).x == 2 && (p * 2).y == 4 &&
                   dot(p, r) == 11 && value(c) == 4 && bumped == 0
               ? 0
               : 1;
}
