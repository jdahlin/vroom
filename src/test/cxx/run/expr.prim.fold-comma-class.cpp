// [expr.prim.fold]/2 with [over.match.oper]/9: a comma fold over operands of
// class type: the built-in comma, or the operator, function overload
// resolution finds.
struct Writer {
    int total = 0;
    Writer &operator<<(int v)
    {
        total = total * 10 + v;
        return *this;
    }
};

struct Tag {
    int v;
};

static Tag operator,(Tag a, Tag b) { return Tag{a.v * 10 + b.v}; }

template <typename... T> static void write_all(Writer &w, T... v)
{
    ((w << v), ...);
}

template <typename... T> static int joined(T... v)
{
    return (..., v).v;
}

template <typename... T> static int right_joined(T... v)
{
    return (v, ...).v;
}

int main()
{
    Writer w;
    write_all(w, 1, 2, 3);
    if (w.total != 123)
        return 1;
    if (joined(Tag{1}, Tag{2}, Tag{3}) != 123)
        return 2;
    if (right_joined(Tag{1}, Tag{2}, Tag{3}) != 1 * 10 + 2 * 10 + 3)
        return 3;
    return 0;
}
