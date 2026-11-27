// [temp.arg.explicit]/9: explicit arguments for a pack that the call's
// arguments cover exactly.
template <typename T, typename... A> static int sum(int k, A &&...a)
{
    T total = k;
    return (total + ... + static_cast<int>(a));
}
template <typename T, typename... A> static int pass(A... a)
{
    return sum<T, A...>(100, static_cast<A &&>(a)...);
}
int main()
{
    long x = 3;
    int r = pass<int, long, char>(x, char(4));
    r += sum<int, long &, int>(1, x, 2);
    r += sum<long>(5);
    r += pass<int>();
    return r - 218;
}
