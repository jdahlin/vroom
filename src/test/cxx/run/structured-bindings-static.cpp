/* [dcl.struct.bind]/1: structured bindings whose variable e has static
   storage duration: at namespace scope (external linkage, internal for
   static or const) and static in a block, initialized dynamically, named
   from other functions; e's symbol is [abi:mangle.name]'s DC <source-name>+
   E (_ZDC1a1bE, _ZN1nDC1g1hEE, _ZZ3usevEDC1m1oE).  A const object
   initialized dynamically is written at start-up: not read-only. */

#define check(e)                                                               \
    do {                                                                       \
        if (!(e))                                                              \
            __builtin_abort();                                                 \
    } while (0)

struct P {
    int x, y;
};

P p{1, 2};
int arr[2] = {5, 6};

auto [a, b] = p;
static auto [c, d] = p;
const auto [e, f] = p;
namespace n {
auto [g, h] = p;
auto &[i, j] = p;
} // namespace n
auto &[k, l] = arr;

int calls;
P next()
{
    calls++;
    return P{calls, calls * 10};
}

int use()
{
    static auto [m, o] = next();
    return m + o;
}

int main()
{
    check(a == 1 && b == 2 && c == 1 && d == 2 && e == 1 && f == 2);
    a = 7;
    check(p.x == 1);
    n::i = 9;
    check(p.x == 9 && n::g == 1 && n::h == 2);
    l = 60;
    check(arr[1] == 60 && k == 5);
    check(use() == 11 && use() == 11 && calls == 1);
    return 0;
}
