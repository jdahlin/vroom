/* [dcl.init]/7, [dcl.constexpr]/10: a constexpr local default-initialized
   by a constructor. */
extern "C" int printf(const char *, ...);

template <int N> struct span {
    int *p;
    constexpr span() : p(nullptr) {}
    constexpr int size() const { return N; }
    constexpr bool empty() const { return !N; }
};
struct two {
    int a, b;
    constexpr two() : a(1), b(2) {}
};

int main()
{
    constexpr span<0> none;
    static_assert(none.empty() && none.size() == 0 && none.p == nullptr);
    constexpr two pairs[3];
    static_assert(pairs[2].b == 2);
    constexpr two t;
    const two *q = &t;
    int array[t.b] = {};
    printf("%d %d %d %zu\n", none.size(), q->a + q->b, pairs[1].a,
           sizeof array / sizeof *array);
    return 0;
}
