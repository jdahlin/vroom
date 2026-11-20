/* [temp.mem]/8, [temp.func.order]: partial ordering of member function templates of one name (a more specialized one is better), with the implicit object parameter, and of a const and a non-const one */
struct S {
    template <class T> int f(T) { return 1; }
    template <class T> int f(T *) { return 2; }
    template <class T> int f(const T &) const { return 3; }
    template <class T, class U> int g(T, U) { return 4; }
    template <class T> int g(T, T) { return 5; }
};
int main()
{
    S s;
    const S c = {};
    int i = 0;
    return s.f(1) - 1 + s.f(&i) - 2 + c.f(1) - 3 + s.g(1, 'a') - 4 + s.g(1, 2) - 5;
}
