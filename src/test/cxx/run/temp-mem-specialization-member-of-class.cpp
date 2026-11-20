/* [temp.expl.spec]/1, [temp.mem]/1: an explicit specialization of a member function template of a class, non-static and static, "template <> int S::f<int>(int)" */
struct S {
    template <class T> int f(T) { return 1; }
    template <class T> static int g(T) { return 2; }
};
template <> int S::f<int>(int) { return 10; }
template <> int S::g<char>(char) { return 20; }
int main()
{
    S s;
    return s.f(1) - 10 + s.f('a') - 1 + S::g('a') - 20 + S::g(1) - 2;
}
