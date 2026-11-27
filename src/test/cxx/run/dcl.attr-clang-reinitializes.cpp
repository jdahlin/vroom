/* [dcl.attr.grammar]/6: clang::reinitializes, for use-after-move checkers
   only, before a member function */
struct A {
    int v = 0;
    [[clang::reinitializes]] void reset(int x) { v = x; }
};
template <class T> struct C {
    T v{};
    [[clang::reinitializes]] T *reset(T x) { v = x; return &v; }
};

int main()
{
    A a;
    a.reset(1);
    C<int> c;
    return a.v + *c.reset(2) - 3;
}
