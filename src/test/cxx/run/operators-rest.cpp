// the comma, ->*, unary & and -> operator functions: [over.binary],
// [over.unary], [over.ref]
#include <stdio.h>

struct Log {
    int sum;
};
Log operator,(Log a, int v) { return Log{a.sum * 10 + v}; }

struct Point {
    int x, y;
};

struct Handle {
    Point *p;
    Point *operator->() { return p; }
    int operator->*(int k) { return p->x * k; }
};

struct Outer {
    Handle h;
    Handle &operator->() { return h; } // applied again: Handle::operator->
};

struct Odd {
    int v;
    int operator&() const { return v + 1000; }
};

int main()
{
    Log l{0};
    Log r = (l, 1, 2, 3);           // 123
    int total = r.sum;
    int plain = (1, 2);             // built-in
    total += plain;
    Point pt{4, 5};
    Handle h{&pt};
    total += h->x + h->y;           // 9
    h->y = 50;
    total += pt.y;                  // 50
    total += h->*3;                 // 12
    Outer o{h};
    total += o->x;                  // 4
    Odd odd{7};
    total += &odd;                  // 1007
    printf("%d\n", total);
    return total & 255;
}
