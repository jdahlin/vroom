// member operator functions defined outside their class: [class.mfct],
// [over.oper]
#include <stdio.h>

struct V {
    int x, y;
    V operator+(V o) const;
    V &operator+=(const V &o);
    bool operator==(const V &o) const;
    int operator[](int i) const;
    int operator()(int k) const;
    V &operator=(const V &o);
};

V V::operator+(V o) const { return V{x + o.x, y + o.y}; }
V &V::operator+=(const V &o)
{
    x += o.x;
    y += o.y;
    return *this;
}
bool V::operator==(const V &o) const { return x == o.x && y == o.y; }
int V::operator[](int i) const { return i ? y : x; }
int V::operator()(int k) const { return x * k + y; }
V &V::operator=(const V &o)
{
    x = o.x * 2;
    y = o.y * 2;
    return *this;
}

int main()
{
    V a{1, 2}, b{3, 4};
    V c = a + b;      // 4, 6
    c += a;           // 5, 8
    V expected{5, 8};
    bool same = c == expected;
    int total = c[0] * 10 + c[1] + same * 100; // 158
    total += a(10);   // 12
    V d{0, 0};
    d = a;            // 2, 4
    total += d.x * 1000 + d.y * 10000;         // 42000
    printf("%d\n", total);
    return total & 255;
}
