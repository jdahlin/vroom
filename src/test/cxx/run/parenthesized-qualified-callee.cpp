/* [over.call.func]/1: a parenthesized qualified-id calls the overload set
   ((std::max)(0, n), written against a max macro) */
namespace lib
{
template <class T> const T &max(const T &a, const T &b)
{
    if (a < b)
        return b;
    return a;
}
template <class T, class C> const T &max(const T &a, const T &b, C less)
{
    if (less(a, b))
        return b;
    return a;
}
int twice(int x)
{
    return 2 * x;
}
long twice(long x)
{
    return 3 * x;
}
} // namespace lib
template <class T> T f(T n)
{
    T three = 3;
    return (lib::max)(T(0), three - n) + ((lib::twice))(n);
}
int main()
{
    return f(1) != 4 || f(1L) != 5 || (lib::max)(4, 2) != 4;
}
