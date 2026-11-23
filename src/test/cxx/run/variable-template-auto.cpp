// an auto variable template with two specializations
template <class T> auto scaled = T(2) * T(3);
template <auto x> constexpr auto twice = x + x;

int main()
{
    if (scaled<int> != 6)
        return 1;
    if (scaled<long> != 6L)
        return 2;
    if (sizeof(scaled<long>) != sizeof(long))
        return 3;
    if (twice<4> != 8)
        return 4;
    if (twice<5L> != 10L)
        return 5;
    return 0;
}
