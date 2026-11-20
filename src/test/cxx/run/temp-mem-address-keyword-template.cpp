/* [temp.mem]/1, [temp.names]/4, [over.over]: &S::template f<long>, the keyword template before a member template's name in a qualified-id */
struct S {
    template <class T> T f(T t) { return t + 1; }
    template <class T> static T s(T t) { return t + 2; }
};
int main()
{
    S o;
    long (S::*pl)(long) = &S::template f<long>;
    return int((o.*pl)(2)) - 3;
}
