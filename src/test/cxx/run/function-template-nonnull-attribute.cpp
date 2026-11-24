// A function template and a member function template may carry
// __attribute__((nonnull(N))) (nlohmann's JSON_HEDLEY_NON_NULL).
template <class T> __attribute__((nonnull(1))) int first(T *p, int n) { return *p + n; }
struct S {
    template <class T> __attribute__((nonnull(2))) static int second(int n, T *p) { return *p + n; }
};
int main()
{
    int x = 4;
    if (first(&x, 1) != 5)
        return 1;
    if (S::second(2, &x) != 6)
        return 2;
    return 0;
}
