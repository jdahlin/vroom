// [dcl.fct.default]/3: default arguments of a lambda's parameters.
struct Opt {
    int a = 3, b = 4;
};
static int base = 10;
static int next() { return ++base; }
int main()
{
    int local = 100;
    auto f = [&](int x, Opt o = {}, bool flag = false) {
        return x + o.a * 10 + o.b + int(flag) * 1000 + local;
    };
    auto g = [](int x = next(), int y = sizeof(Opt)) { return x * 100 + y; };
    int r = f(1) + f(2, Opt{1, 2}, true);
    int s = g();
    s += g();
    s += g(1);
    s += g(1, 1);
    return (r - (135 + 1114)) + (s - (1108 + 1208 + 108 + 101));
}
