/* Assignments where the implicit copy assignment is or is not chosen. */
extern "C" int printf(const char *, ...);

struct nothing {};

/* [over.match.funcs]/4: a const left operand has no implicit operator= */
struct sink {
    template <class T> const sink &operator=(const T &) const { return *this; }
};
constexpr sink ignore_it{};

enum E { A, B };

/* [over.match.viable]/2: no declared operator= viable, the right operand
   is converted and the implicit (here defaulted) one assigns */
struct box {
    int v;
    box(int a) : v(a) {}
    box(E e) : v(10 + e) {}
    box(const box &) = default;
    box &operator=(const box &) = default;
    box &operator=(box &&) = default;
    box &operator=(nothing)
    {
        v = -1;
        return *this;
    }
};

int main()
{
    ignore_it = 3;
    ignore_it = B;
    box x(1), y(2);
    x = 5;
    y = B;
    int a = x.v, b = y.v;
    x = nothing();
    printf("%d %d %d\n", a, b, x.v);
    return !(a == 5 && b == 11 && x.v == -1);
}
