/* [dcl.init.ref], [basic.type.qualifier]/3: a reference to an array of
   const is a const reference, so it binds an xvalue array */
using A = int[3];
int a[3] = {1, 2, 3};
A &&moved()
{
    return static_cast<A &&>(a);
}
int plain(const int (&r)[3])
{
    return r[0];
}
long plain(...)
{
    return 100;
}
template <class T, unsigned long N> int deduced(const T (&r)[N])
{
    return r[N - 1] * 10;
}
long deduced(...)
{
    return 200;
}
int main()
{
    const int (&r)[3] = moved();
    return !(plain(moved()) == 1 && deduced(moved()) == 30 && r[1] == 2);
}
