/* [temp.deduct.call]/1 with [temp.arg.explicit]/7: a braced list for an
   array parameter whose element type is explicitly specified converts each
   element; only the bound is deduced.  A specified bound is not deduced
   from the length. */
template <class T, int N> int rv(T (&&a)[N]) { return N * 10 + (int)sizeof(a[0]); }
template <class T, int N> int sum(T (&&a)[N])
{
    int s = 0;
    for (int i = 0; i < N; i++)
        s = s * 10 + a[i].x + a[i].y;
    return s;
}
struct point { int x, y; };

int main()
{
    if (rv<signed char>({1, -2, 3}) != 31)
        return 1;
    if (rv<char, 5>({1, 2}) != 51)
        return 2;
    if (sum<point>({{1, 2}, {3, 4}}) != 37)
        return 3;
    return 0;
}
