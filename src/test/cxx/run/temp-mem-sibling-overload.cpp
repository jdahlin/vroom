/* [temp.mem]/2, Example 3: a non-template member function and a member function template of one name (a use that fits both refers to the non-template member unless an explicit template argument list is supplied) */
template <class T> struct A {
    int f(int) { return 1; }
    template <class T2> int f(T2) { return 2; }
};
struct P {
    int f(int) { return 1; }
    template <class T2> int f(T2) { return 2; }
};
int main()
{
    A<char> ac;
    P p;
    return ac.f(1) - 1 + ac.f('c') - 2 + ac.f<>(1) - 2 + p.f(1) - 1 + p.f('c') - 2 + p.f<>(1) - 2;
}
