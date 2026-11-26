/* [class.copy.assign], [over.match.oper]: a defaulted copy assignment
   beside an operator= template that is not viable for the class itself */
template <bool, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> { typedef T type; };
struct Opt {
    int v;
    Opt &operator=(const Opt &) = default;
    Opt &operator=(Opt &&) = default;
    template <class U, typename enable_if<sizeof(U) == 1, int>::type = 0>
    Opt &operator=(U &&u)
    {
        v = u + 100;
        return *this;
    }
};
int main()
{
    Opt a{1}, b{2};
    a = b;
    if (a.v != 2)
        return 1;
    a = Opt{3};
    if (a.v != 3)
        return 2;
    a = 'a';
    return a.v != 'a' + 100;
}
