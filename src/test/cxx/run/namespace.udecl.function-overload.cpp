// [namespace.udecl]/12, [over]: a using-declaration of a function in a
// namespace beside a function of the same name (e.g. Catch2's
// `using ::operator<<;` in `namespace Catch`).

struct Out {
    int sum = 0;
};

struct TagA {
    int v;
};

struct TagB {
    int v;
};

Out &operator<<(Out &o, TagA a)
{
    o.sum += a.v;
    return o;
}

namespace N {
Out &operator<<(Out &o, TagB b)
{
    o.sum += b.v * 10;
    return o;
}

using ::operator<<;

int run()
{
    Out o;
    o << TagA{3} << TagB{4};
    return o.sum;
}
} // namespace N

int main()
{
    if (N::run() != 43)
        return 1;
    return 0;
}
