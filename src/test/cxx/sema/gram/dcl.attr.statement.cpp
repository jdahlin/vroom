// [stmt.pre]/1, [dcl.attr.grammar]/6: attributes before a statement that
// change nothing vcc generates, or that clang does not know (ignored with a
// warning).
int f(int a)
{
    [[unknown_thing]] a = 2;
    [[vendor::unknown(1)]];
    [[unlikely]] if (a > 3)
        a = 1;
    [[foo]] int q = a;
    switch (a) {
    case 1:
        a++;
        [[fallthrough]];
    case 2:
        break;
    }
    return a + q;
}
