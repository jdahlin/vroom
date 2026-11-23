/* [temp.mem]/2, Example 3: A<int>::f(int) specialized as a non-template member and as a member function template specialization ("template <> template <> void A<int>::f<>(int)") */
template <class T> struct A {
    int f(int) { return 1; }
    template <class T2> int f(T2) { return 2; }
};
template <> int A<int>::f(int) { return 10; }
template <> template <> int A<int>::f<>(int) { return 20; }
int main()
{
    A<int> ai;
    return ai.f(1) - 10 + ai.f<>(1) - 20 + ai.f('c') - 2;
}
