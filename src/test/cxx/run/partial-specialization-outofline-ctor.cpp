// [temp.class.spec.mfunc], [class.base.init]/2: a constructor of a partial
// specialization defined outside it names the specialization's own members
// (not the primary template's) in its mem-initializers
template <class T, class A> struct V {
    int x;
    V();
};
template <class T, class A> V<T, A>::V() : x(1) {}

template <class A> struct V<bool, A> {
    int y;
    long z;
    V() noexcept;
    explicit V(int n);
    void set() noexcept;
};
template <class A> inline V<bool, A>::V() noexcept : y(2), z(3) {}
template <class A> V<bool, A>::V(int n) : y(n), z(n * 2) {}
template <class A> void V<bool, A>::set() noexcept { y = 9; }

int main() {
    V<int, int> a;
    V<bool, int> b;
    V<bool, char> c(5);
    if (a.x != 1)
        return 1;
    if (b.y != 2 || b.z != 3)
        return 2;
    if (c.y != 5 || c.z != 10)
        return 3;
    c.set();
    if (c.y != 9)
        return 4;
    return 0;
}
