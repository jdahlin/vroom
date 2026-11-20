// [dcl.attr.fallthrough] and [ext:gnu_attributes] the fallthrough statement
extern "C" int printf(const char *, ...);

static int pick(int x)
{
    int r = 0;
    switch (x) {
    case 0:
        r += 1;
        [[fallthrough]];
    case 1:
        r += 2;
        __attribute__((fallthrough));
    case 2:
        r += 4;
        [[gnu::fallthrough]];
    case 3:
        r += 8;
        [[clang::fallthrough]];
    default:
        r += 16;
    }
    return r;
}

int main()
{
    for (int i = 0; i < 5; i++)
        printf("%d\n", pick(i));
    return 0;
}
