// [class.nest]: classes defined among the members of another one.  The
// members declared before a nested class and the ones after it are one
// class's members, in declaration order ([class.mem]/19), however many
// nested classes lie between them.
struct Outer {
    int a0, a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11;
    struct First { int x, y; };
    First first;
    struct Second { int x, y, z; };
    Second second;
    int b0, b1, b2;
    struct Third {
        int x;
        struct Innermost { int p, q, r; };
        Innermost innermost;
        int y;
    };
    Third third;
    struct Fourth { char c; };
    Fourth fourth;
    int c0, c1;
    struct Fifth { long wide; };
    Fifth fifth;
    struct Sixth { int x, y, z, w; };
    Sixth sixth;
    int last;
};

int main() {
    Outer o;
    o.a0 = 1;
    o.a5 = 2;
    o.a11 = 3;
    o.first.x = 4;
    o.first.y = 5;
    o.second.z = 6;
    o.b0 = 7;
    o.b2 = 8;
    o.third.x = 9;
    o.third.innermost.q = 10;
    o.third.y = 11;
    o.fourth.c = 12;
    o.c0 = 13;
    o.c1 = 14;
    o.fifth.wide = 15;
    o.sixth.w = 16;
    o.last = 17;
    int failures = 0;
    failures += o.a0 + o.a5 + o.a11 != 6;
    failures += o.first.x + o.first.y + o.second.z != 15;
    failures += o.b0 + o.b2 != 15;
    failures += o.third.x + o.third.innermost.q + o.third.y != 30;
    failures += o.fourth.c + o.c0 + o.c1 != 39;
    failures += o.fifth.wide + o.sixth.w + o.last != 48;
    /* declaration order: each member lies after the ones before it */
    failures += !(&o.a0 < &o.a11 && &o.a11 < &o.b0 && &o.b0 < &o.b2);
    failures += !(&o.b2 < &o.c0 && &o.c0 < &o.c1 && &o.c1 < &o.last);
    failures += &o.a11 - &o.a0 != 11 || &o.b0 - &o.a0 != 17;
    failures += &o.c0 - &o.a0 != 26 || &o.last - &o.a0 != 34;
    failures += sizeof(Outer) != 36 * sizeof(int);
    failures += sizeof(Outer::Third) != 5 * sizeof(int);
    return failures;
}
