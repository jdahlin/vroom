/* [temp.param]/8: a parameter of a function or array type that depends on
   earlier parameters is adjusted after substitution too. */
extern "C" int printf(const char *, ...);
template <typename T, bool (LESS)(const T &, const T &)>
int count_less(const T *a, int n, const T &key)
{
    int c = 0;
    for (int i = 0; i < n; i++)
        c += LESS(a[i], key);
    return c;
}
template <class T, int f(T)> struct Apply {
    int operator()(T v) const { return f(v) + 1; }
};
template <class T, T data[]> int first() { return static_cast<int>(data[0]); }
bool less(const int &a, const int &b) { return a < b; }
bool greater(const int &a, const int &b) { return a > b; }
int twice(int v) { return 2 * v; }
long table[] = {42, 7};
int main()
{
    int a[] = {1, 5, 9, 3};
    printf("%d %d %d %d\n", count_less<int, less>(a, 4, 5),
           count_less<int, greater>(a, 4, 2), Apply<int, twice>{}(20),
           first<long, table>());
}
