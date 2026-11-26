// [dcl.init.aggr]/5.1: default member initializer of a class member with
// constructors and a destructor in an aggregate (e.g. `GroupStatistics`).

static int dtors = 0;

struct Map {
    int count;
    Map() : count(3) {}
    ~Map() { dtors++; }
};

struct Stats {
    int a = 1;
    Map m{};
    int b = 2;
};

int main()
{
    {
        Stats s{};
        if (s.a != 1 || s.m.count != 3 || s.b != 2)
            return 1;
    }
    if (dtors != 1)
        return 2;
    return 0;
}
