/* [temp.mem]/1, [over.over], [temp.deduct.funcaddr]: the address of a static member function template's specialization, &S::s<int>, and S::s<int> converted to a pointer to function */
struct S {
    template <class T> T f(T t) { return t + 1; }
    template <class T> static T s(T t) { return t + 2; }
};
int main()
{
    int (*ps)(int) = &S::s<int>;
    int (*pf)(int) = S::s<int>;
    return ps(3) - 5 + pf(4) - 6;
}
