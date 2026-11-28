/* bench/debug-values.py: C++ parameters, this, references, bases and
   members print as from clang's program */
namespace ns
{
struct Base {
    int b;
    virtual int v() { return b; }
};
int hidden = 6;
} // namespace ns

struct S : ns::Base {
    int m;
    int &r;
    S(int &x) : m(2), r(x) { b = 1; }
};

template <class T> struct Box {
    T item;
};

struct Counter {
    int m;
    static int count;
    static int twice(int a) { return 2 * a; }
    int get(int d) const { return m + d; }
};
int Counter::count = 4;

namespace outer
{
namespace
{
int anonymous = 8;
}
template <class T, int N> struct Many {
    T item[N];
};
} // namespace outer

int total = 5;

struct Root {
    int root;
};
struct Left : virtual Root {
    int left;
};
struct Right : virtual Root {
    int right;
};
struct Diamond : Left, Right {
    int own;
};
Diamond diamond;

namespace outer
{
enum class Color : short { red = 1, green = 3 };
}
enum Plain { minus = -2, plus = 7 };
outer::Color shade = outer::Color::green;
Plain plains[2] = {plus, minus};

__attribute__((noinline)) static int use(const S &s, Box<long> box, S *p,
                                         int &&moved)
{
    outer::Many<Box<long>, 2> many{{{1}, {2}}};

    int x = s.m + (int)box.item;
    const int &alias = x;

    int sum = x + total + ns::hidden + p->b + moved + alias;

    Counter counter{5};

    diamond.root = 21;
    diamond.right = 22;
    Right &through = diamond;

    sum += outer::anonymous + (int)many.item[1].item;
    sum += counter.get(1) + Counter::twice(2) + Counter::count;
    return sum; /* STOP */
}

int main()
{
    int v = 7;
    S s(v);
    Box<long> box{9};

    return use(s, box, &s, 4) == 0;
}
/* PRINT s.m s.b s.r box.item p->m p->b moved x alias total ns::hidden outer::anonymous shade plains Counter::count counter diamond.root diamond.right through.root */
/* WHATIS box many s p shade plains counter */
