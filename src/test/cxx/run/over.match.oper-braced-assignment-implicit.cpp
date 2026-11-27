/* cxx-run-flags: -std=c++20 */
/* [expr.ass]/8, [over.ics.list]: x = {...} of a class with implicit
   assignment operators that are not trivial beside other operator=
   functions (std::optional's shape), with a designated list bound to an
   rvalue reference */
extern "C" int printf(const char *, ...);

static int copies, moves;

struct Counting {
    Counting() = default;
    Counting(const Counting &) = default;
    Counting &operator=(const Counting &)
    {
        copies++;
        return *this;
    }
    Counting &operator=(Counting &&)
    {
        moves++;
        return *this;
    }
};

struct Value {
    float first = 1;
    int second = 2;
};

struct None {
    explicit None(int) {}
};

struct Optional {
    Value value;
    Counting counting;
    int kind = 0;
    Optional() = default;
    Optional(Value &&v) : value(v), kind(1) {}
    Optional &operator=(None)
    {
        kind = 2;
        return *this;
    }
};

struct Pair {
    int a, b;
};
struct Takes {
    int sum = 0;
    Counting counting;
    Takes &operator=(Pair &&p)
    {
        sum = p.a * 10 + p.b;
        return *this;
    }
};

int main()
{
    Optional o;
    o = {{.second = 7}};
    printf("%d %d %d %d %d\n", (int)o.value.first, o.value.second, o.kind,
           copies, moves);
    Takes t;
    t = {.a = 3, .b = 4};
    printf("%d\n", t.sum);
    return 0;
}
