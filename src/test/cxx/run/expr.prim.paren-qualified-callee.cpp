// [expr.prim.paren]: a parenthesized qualified-id calls what the name finds,
// overloads and templates included ((std::max)(a, b), as {fmt} writes it)
namespace n
{
template <class T> T most(T a, T b)
{
    if (a < b)
        return b;
    return a;
}
template <class T> T most(T a, T b, T c)
{
    return most(most(a, b), c);
}
int pick(int)
{
    return 1;
}
int pick(char)
{
    return 2;
}
} // namespace n
template <class T> int f(T x)
{
    int bound = 3;
    return (n::most)(0, bound - x);
}
int main()
{
    if (f(1) != 2 || ((n::most))(1, 5, 4) != 5)
        return 1;
    if ((n::pick)('a') != 2 || (n::most<long>)(2, 7) != 7)
        return 2;
    return 0;
}
