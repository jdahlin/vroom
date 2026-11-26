// [class.base.init], [dcl.init.aggr]: default member initializer of an
// aggregate class with a base class that has a constructor/destructor
// (e.g. `NetworkAuthorizedKeys allow_list{};` in `CompanyProperties`).

static int dtors = 0;

struct Vec {
    int len;
    Vec() : len(5) {}
    ~Vec() { dtors++; }
};

struct KeyList : Vec {
    int extra = 2;
};

struct Props {
    KeyList allow_list{};
    Props() = default;
};

int main()
{
    {
        Props p;
        if (p.allow_list.len != 5 || p.allow_list.extra != 2)
            return 1;
    }
    if (dtors != 1)
        return 2;
    return 0;
}
