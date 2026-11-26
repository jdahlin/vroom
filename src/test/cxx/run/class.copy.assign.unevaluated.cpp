// [class.copy.assign]/12: unevaluated implicit class assignment (e.g.
// __is_assignable / decltype(a = b)) on a class whose subobject has a
// user-defined operator=.

struct Member {
    int v;
    Member &operator=(const Member &o)
    {
        v = o.v;
        return *this;
    }
};

struct Outer {
    int tag;
    Member m;
};

static_assert(__is_assignable(Outer &, const Outer &));
static_assert(!__is_assignable(const Outer &, const Outer &));

int main()
{
    Outer a{1, {2}};
    if (a.tag != 1 || a.m.v != 2)
        return 1;
    return 0;
}
