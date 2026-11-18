/* Variable templates of namespace scope: constexpr ones usable in
   constant expressions, others defined once per specialization (weak,
   mangled _Z4zeroIiE), defaults and non-type parameters, a
   specialization written to and read back ([temp.pre], [temp.inst]). */
#include <stdio.h>

template <int N> constexpr int square = N * N;
template <class T> constexpr int size_of = sizeof(T);
template <class T> T zero = T();
template <class T, int N = 2> int scaled = sizeof(T) * N;
template <class T> constexpr T limit = T(100);

static_assert(square<5> == 25, "square");
static_assert(size_of<long> == 8, "size_of");

int table[square<3>];

int bump()
{
    zero<int> += 1;
    return zero<int>;
}

int main()
{
    int fail = 0;
    if (sizeof table != 9 * sizeof(int))
        fail |= 1;
    if (bump() != 1 || bump() != 2 || zero<int> != 2)
        fail |= 2;
    if (zero<long> != 0)
        fail |= 4;
    if (scaled<short> != 4 || scaled<char, 3> != 3)
        fail |= 8;
    scaled<short> = 7;
    if (scaled<short, 2> != 7)
        fail |= 16;
    if (limit<int> != 100 || limit<char> != 100)
        fail |= 32;
    int *p = &zero<int>;
    if (*p != 2)
        fail |= 64;
    printf("%d\n", fail);
    return fail;
}
