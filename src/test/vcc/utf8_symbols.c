#include <stdlib.h>
int привет = 7;
int Lefèvre = 11;
static int café(void)
{
    static int счетчик = 20;
    return ++счетчик;
}
static int mañana(void)
{
    static int счетчик = 40;
    return ++счетчик;
}
int (*调用)(void) = café;
int *指针 = &привет;
int main(void)
{
    if (*指针 != 7 || Lefèvre != 11 || 调用() != 21 || café() != 22 ||
        mañana() != 41 || mañana() != 42)
        abort();
    return 0;
}
