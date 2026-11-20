/* [expr.prim.lambda.capture]/6: init-captures */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

struct move_only {
    int v;
    move_only(int v) : v(v) {}
    move_only(move_only &&o) : v(o.v) { o.v = -1; }
    move_only(const move_only &) = delete;
};

int main()
{
    int x = 4;
    auto a = [y = x + 1] { return y; };
    auto b = [&r = x] { r += 1; return r; };
    auto c = [y = x * 2]() mutable { y += 1; return y; };
    move_only m(9);
    auto d = [k = static_cast<move_only &&>(m)] { return k.v; };
    check(a() == 6);
    check(b() == 6 && x == 6);
    check(c() == 13 && c() == 14);
    check(d() == 9 && m.v == -1);
    auto e = [x, z = x + 1] { return x + z; };
    check(e() == 13);
    auto f = [n = 3] { return [n] { return n + 1; }(); };
    check(f() == 4);
    return 0;
}
