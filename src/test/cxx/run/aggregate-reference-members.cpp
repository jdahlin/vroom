/* [class.default.ctor]/2: an aggregate with reference members has a deleted
   default constructor, which is no definition; a braced list initializes it
   ([dcl.init.aggr]), a default member initializer beside the references */
extern "C" int printf(const char *, ...);
namespace {
struct checker {
    const int &a;
    int &b;
    unsigned long problems = 0;
    void note(int d) { problems += d; b += a; }
};
struct plain {
    int &r;
    const int c;
};
}
int main()
{
    int x = 3, y = 10;
    checker c{x, y};
    c.note(2);
    c.note(5);
    plain p{y, 4};
    p.r += p.c;
    printf("%lu %d %d\n", c.problems, y, p.c);
    return (int)c.problems + y - 7 - 20;
}
