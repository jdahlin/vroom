// [temp.arg.nontype]/2: a cast of nullptr to the parameter's pointer type,
// as a default template argument
struct V {
    static bool Left(const V &a, const V &b) { return a.x < b.x; }
    int x;
};
int slot;
int *index_of(const V &) { return &slot; }

template <class T, bool (*L)(const T &, const T &),
          int *(*I)(const T &) = (int *(*)(const T &))nullptr>
struct Q {
    int less(const T &a, const T &b)
    {
        if (I != nullptr)
            *I(a) += 10;
        return L(a, b);
    }
};

int main()
{
    Q<V, V::Left> q;
    Q<V, V::Left, index_of> r;
    V a{1}, b{2};
    int x = q.less(a, b), y = r.less(b, a);
    __builtin_printf("%d %d %d\n", x, y, slot);
    return 0;
}
