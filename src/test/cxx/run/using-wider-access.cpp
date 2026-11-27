/* [namespace.udecl]/19: a using-declaration gives the member its own access */
extern "C" int printf(const char *, ...);

struct visitor {
protected:
    virtual int visit(int x)
    {
        return x + 1;
    }
    int twice(int x)
    {
        return 2 * x;
    }
    int twice(int x, int y)
    {
        return 2 * x + y;
    }
};

struct structure : visitor {
public:
    using visitor::visit;
    using visitor::twice;
};

struct overriding : visitor {
public:
    using visitor::visit;
    int visit(int x) override
    {
        return x + 10;
    }
};

struct further : structure {};

int main()
{
    structure s;
    overriding o;
    further f;
    int a = s.visit(1), b = s.twice(2), c = s.twice(2, 3), d = o.visit(1),
        e = f.twice(5);
    printf("%d %d %d %d %d\n", a, b, c, d, e);
    return !(a == 2 && b == 4 && c == 7 && d == 11 && e == 10);
}
