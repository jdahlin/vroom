/* An init-capture in a lambda with a trailing-return-type that does not
   name it ([expr.prim.lambda.capture]/6), and a class's members declared
   after a variable template's initializer instantiated another class
   within its member-specification ([class.mem]/1). */
struct pair {
    int space, comma;
};
static const pair output = {1, 2};

static auto separator()
{
    return [first = true]() mutable -> const int & {
        if (first) {
            first = false;
            return output.space;
        }
        return output.comma;
    };
}

template <class T> struct V {
    struct {
        T *p = nullptr;
        int q = 3;
    };
    int r = 4;
};
template <class T> struct A {
    static constexpr bool value = sizeof(V<T>) > 0;
};
template <class T> inline constexpr bool complete = A<T>::value;
template <class T> struct N {
    static_assert(complete<T>);
    int first = 5;
    unsigned char s[sizeof(V<T>)];
    int last = 6;
};

int main()
{
    auto s = separator();
    if (s() != 1 || s() != 2 || s() != 2)
        return 1;
    int base = 10;
    auto add = [k = base + 1, &b = base](int x) -> int { return x + k + b; };
    if (add(1) != 22)
        return 2;
    N<int> n;
    if (n.first != 5 || n.last != 6)
        return 3;
    V<int> v;
    if (v.p || v.q != 3 || v.r != 4)
        return 4;
    return 0;
}
