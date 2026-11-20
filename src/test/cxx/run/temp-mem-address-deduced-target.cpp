/* [temp.mem]/1, [temp.deduct.funcaddr]/1: the target type of a pointer to member function initialization deduces the template arguments of &S::f */
struct S {
    template <class T> T f(T t) { return t + 1; }
    template <class T> static T s(T t) { return t + 2; }
};
int main()
{
    S o;
    char (S::*pd)(char) = &S::f;
    return int((o.*pd)('a')) - 98;
}
