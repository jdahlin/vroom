// operator functions: [over.oper], [over.match.oper], [over.unary],
// [over.binary], [over.inc]
#include <stdio.h>

struct V {
    int x, y;
    V &operator+=(V o)
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    V operator-() const { return V{-x, -y}; }
    bool operator==(V o) const { return x == o.x && y == o.y; }
    V &operator++()
    {
        ++x;
        return *this;
    }
    V operator++(int)
    {
        V old = *this;
        ++y;
        return V{old.x, old.y};
    }
};

// non-member, by value and by reference
V operator+(V a, V b) { return V{a.x + b.x, a.y + b.y}; }
V operator*(const V &a, int k) { return V{a.x * k, a.y * k}; }
V operator*(int k, const V &a) { return a * k; }
int operator*(const V &a, const V &b) { return a.x * b.x + a.y * b.y; }
bool operator<(const V &a, const V &b) { return a.x < b.x; }
bool operator!(V a) { return !a.x && !a.y; }
V &operator--(V &a)
{
    --a.x;
    return a;
}
V operator--(V &a, int)
{
    V old = a;
    --a.y;
    return V{old.x, old.y};
}

namespace geo {
struct P {
    int v;
};
// found by argument-dependent lookup
int operator%(P a, P b) { return a.v % b.v; }
P operator<<(P a, int s) { return P{a.v << s}; }
} // namespace geo

enum Color { red, green, blue };
Color operator+(Color c, int k) { return static_cast<Color>((int(c) + k) % 3); }
int operator~(Color c) { return 10 + int(c); }

struct Flag {
    bool on;
};
bool operator&&(Flag a, Flag b) { return a.on && b.on; }
int operator|(Flag a, int b) { return int(a.on) | b; }

static int sum(V v) { return v.x * 10 + v.y; }

int main()
{
    V a{1, 2}, b{3, 4};
    int total = sum(a + b);         // 46
    total += sum(a * 2) + sum(3 * a); // 24 + 36
    total += a * b;                 // 11
    V c = a;
    c += b;
    c += V{1, 1};
    total += sum(c);                // 57
    total += sum(-a);               // -12
    total += (a == V{1, 2}) + (a == b) * 100; // 1
    bool less = a < b, more = b < a;
    total += less + more * 100;     // 1
    total += !V{0, 0} + !a * 100;   // 1
    V d{5, 5};
    ++d;
    V e = d++;
    total += sum(d) + sum(e);       // 66 + 65
    --d;
    V f = d--;
    total += sum(d) + sum(f);       // 55 + 56
    total += sum(a + b + c);        // 4*10+6 + 57 = 103
    geo::P p{17}, q{5};
    total += p % q + (p << 2).v;    // 2 + 68
    Color k = blue + 2;             // green
    total += int(k) + ~k + ~red;    // 1 + 11 + 10
    total += int(red + 1) * 1000;   // 1000
    int plain = 2 + 3;              // built-in
    total += plain;
    Flag on{true}, off{false};
    total += (on && on) + (on && off) * 100 + (off | 4); // 1 + 4
    printf("%d\n", total);
    return total & 255;
}
