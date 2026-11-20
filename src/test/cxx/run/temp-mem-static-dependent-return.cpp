/* [temp.mem]/1: static member function templates with a dependent return type, a default template argument and a constraint */
struct S {
    template <class T> static T f(T t) { return t + 1; }
    template <class T, class U = T> static U g(T t) { return U(t) + 1; }
    template <class T> static typename T::type h(T) { return 7; }
    template <class T> requires(sizeof(T) == 4) static int c(T) { return 1; }
    template <class T> requires(sizeof(T) == 1) static int c(T) { return 2; }
    template <class T> static decltype(auto) d(T &t) { return (t); }
};
struct has { using type = int; };
int main()
{
    int x = 4;
    return S::f(1) - 2 + S().g<int>(3) - 4 + S::g<int, long>(3) - 4 + S::h(has()) - 7 + S::c(1) + S::c('a') - 3 + S::d(x) - 4;
}
