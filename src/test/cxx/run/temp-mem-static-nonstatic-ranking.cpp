/* [over.match.funcs]/4, [over.match.best]/2, [temp.func.order]/3: static and
   non-static member function templates of one name ranked together, cv- and
   ref-qualified; the exit status is clang's choice. */
struct S {
    template <class T> static int f(T) { return 1; }
    template <class T> int f(T *) const { return 2; }
    template <class T> static int g(T *) { return 10; }
    template <class T> int g(T) { return 20; }
    template <class T> static int h(T, int) { return 100; }
    template <class T> int h(T, T) && { return 200; }
    template <class T> static int k(const T &) { return 1000; }
    template <class T> int k(T &) & { return 2000; }
};
int main()
{
    S s;
    const S c{};
    int i = 0;
    int r = s.f(&i) + c.f(&i) + s.g(&i) + c.g(&i) + s.h(1, 2) + s.k(i) + c.k(i);
    return r % 251;
}
