template <class T> class box {
    T v;
public:
    box(T x) : v(x) {}
    friend bool operator==(const box &a, const box &b) { return a.v == b.v; }
    friend T get(const box &b) { return b.v; }
    friend int peek(const box &b);
};
int peek(const box<int> &b) { return b.v + 1; }
int main()
{
    box<int> a(3), b(3);
    box<long> c(4);
    if (!(a == b)) return 1;
    if (get(c) != 4) return 2;
    if (peek(a) != 4) return 3;
    return get(a) - 3;
}
