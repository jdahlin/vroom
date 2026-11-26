// [dcl.spec.auto]/3, [dcl.fct]/2: auto with a trailing-return-type in an
// alias-declaration's defining-type-id (e.g. fmt/base.h:1669).

template <class Char>
struct Checker {
    using parse_func = auto (*)(int &) -> const Char *;
    parse_func fn;
};

static const char *parse_one(int &x)
{
    x += 3;
    return "ok";
}

int main()
{
    Checker<char> c{&parse_one};
    int v = 4;
    const char *s = c.fn(v);
    if (v != 7 || s[0] != 'o' || s[1] != 'k')
        return 1;
    return 0;
}
