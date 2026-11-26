// [expr.prim.lambda]: attributes on a lambda-expression before the
// lambda-declarator or compound-statement (P2173R1, used by libc++ <format>).

int main()
{
    int x = 40;
    int a = [&] [[__gnu__::__pure__]] { return x + 2; }();
    if (a != 42)
        return 1;
    int b = [&] [[nodiscard]] (int y) { return x + y; }(5);
    if (b != 45)
        return 2;
    return 0;
}
