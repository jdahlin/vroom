// [ext:gnu_attributes] constructor and destructor, with and without a
// priority: entries of .init_array and .fini_array (-O0's symbols and
// sections are clang's)
extern "C" int printf(const char *, ...);
static int order[8];
static int count;

__attribute__((constructor)) static void late(void)
{
    order[count++] = 3;
}
__attribute__((constructor(102))) static void second(void)
{
    order[count++] = 2;
}
__attribute__((__constructor__(101))) static void first(void)
{
    order[count++] = 1;
}
// a constructor not static, a destructor of each kind
extern "C" __attribute__((constructor, used)) void exported(void)
{
    order[count++] = 4;
}
__attribute__((destructor)) static void last(void)
{
    printf("last\n");
}
__attribute__((destructor(103))) static void before(void)
{
    printf("before\n");
}
__attribute__((destructor(104))) static void earliest(void)
{
    printf("earliest\n");
}

int main()
{
    // priority order first, then the default's in declaration order
    for (int i = 0; i < count; i++)
        printf("%d\n", order[i]);
    return count == 4 && order[0] == 1 && order[1] == 2 ? 0 : 1;
}
