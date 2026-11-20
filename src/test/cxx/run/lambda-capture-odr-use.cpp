/* [basic.def.odr]/4, /10: a variable that is not odr-used is not captured
   by a capture-default; a lambda need not capture a constant variable,
   sizeof, decltype */
#define check(e) do { if (!(e)) __builtin_abort(); } while (0)

int zero = 0;

int main()
{
    const int k = 5;
    constexpr int cx = 7;
    int n = 3;
    auto f = [=] { return k + cx + sizeof(n) + sizeof(int[k]); };
    check(f() + zero == 5 + 7 + sizeof(int) + 5 * sizeof(int));
    check(sizeof(f) == 1);
    auto g = [=] { decltype(n) m = 2; return m; };
    check(g() + zero == 2 && sizeof(g) == 1);
    auto h = [&] { return cx; };
    check(h() + zero == 7 && sizeof(h) == 1);
    /* odr-used: bound to a reference */
    auto r = [=] { const int &x = k; return x; };
    check(r() + zero == 5);
    auto t = [=] { return n; };
    check(sizeof(t) == sizeof(int));
    int arr[3] = {1, 2, 3};
    auto ar = [&] { return arr[1]; };
    auto ac = [=] { return arr[2]; };
    check(ar() == 2 && ac() == 3 && sizeof(ac) == sizeof(arr));
    return 0;
}
