// operator functions that are members alone: [over.ass], [over.call],
// [over.sub]
#include <stdio.h>

struct Counter {
    int value;
    int assigned;
    Counter &operator=(const Counter &o)
    {
        value = o.value;
        assigned++;
        return *this;
    }
    Counter &operator=(int v)
    {
        value = v;
        assigned += 10;
        return *this;
    }
};

struct Table {
    int cells[4];
    int &operator[](int i) { return cells[i & 3]; }
    int operator[](unsigned i) const { return cells[i & 3] * 100; }
};

struct Adder {
    int base;
    int operator()(int x) const { return base + x; }
    int operator()(int x, int y) const { return base + x * y; }
    int operator()() const { return base * 2; }
    int operator()(double x) const { return int(x) * 3; }
};

static int apply(const Adder &f, int v) { return f(v); }

int main()
{
    Counter a{1, 0}, b{2, 0};
    a = b;                     // the copy assignment operator: assigned 1
    a = 7;                     // operator=(int): assigned 11
    (a = b) = 5;               // 11 + 1 + 10 = 22
    int total = a.value + a.assigned;     // 5 + 22
    Table t{{1, 2, 3, 4}};
    t[1] = 20;
    t[2] += 10;
    const Table &ct = t;
    total += t[0] + t[1] + t[2] + ct[3u] / 100; // 1 + 20 + 13 + 4
    Adder add{10};
    total += add(5) + add(2, 3) + add() + add(2.5) + apply(add, 1); // 15 + 16 + 20 + 6 + 11
    printf("%d\n", total);
    return total & 255;
}
