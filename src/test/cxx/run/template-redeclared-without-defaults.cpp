/* [dcl.fct.default]: a function template's redeclaration without defaults
   (libc++'s friend regex_search) leaves them to its calls */
extern "C" int printf(const char *, ...);
template <class T> struct box;
template <class T> int find(T a, const box<T> &b, int flags = 5)
{
    return (int)a + b.v + flags;
}
template <class T> struct box {
    int v = 3;
    template <class U> friend int find(U, const box<U> &, int);
};
int main()
{
    box<int> b;
    printf("%d %d\n", find(1, b), find(2, b, 0));
}
