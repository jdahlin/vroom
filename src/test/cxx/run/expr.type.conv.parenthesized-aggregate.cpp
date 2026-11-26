// [dcl.init]/17.6.2.2, [expr.type.conv] (C++20 P0960R3): parenthesized
// aggregate initialization in functional cast syntax (e.g.
// `std::array<T, 2>({a, b})` and `Pair(1, 2)`).

struct Pair {
    int a;
    int b;
};

template <class T, int N> struct Arr {
    T elems[N];
};

int main()
{
    Pair p = Pair(10, 20);
    if (p.a != 10 || p.b != 20)
        return 1;
    Arr<int, 2> a = Arr<int, 2>({3, 4});
    if (a.elems[0] != 3 || a.elems[1] != 4)
        return 2;
    return 0;
}
