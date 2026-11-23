/* [class.static.data]/4: a const static data member of enumeration type
   (scoped or not, in a class or a class template) with an in-class
   initializer is usable in constant expressions, as is a constexpr one;
   one odr-used is defined at namespace scope without an initializer. */
enum E { a, b, c };
enum class S : char { x, y };

struct X {
    static const E v = b;
    static constexpr E w = c;
    static const S s = S::y;
    static constexpr S t = S::x;
};
const E X::v;

template <class T> struct Y {
    static const E v = c;
    static constexpr S s = S::y;
};

static_assert(X::v == b, "");
static_assert(X::w == c, "");
static_assert(X::s == S::y, "");
static_assert(X::t == S::x, "");
static_assert(Y<int>::v == c, "");
static_assert(Y<int>::s == S::y, "");

int table[X::w + 1];

int read(const E &r) { return r; }

int main()
{
    if (read(X::v) != 1)
        return 1;
    if (sizeof(table) != 3 * sizeof(int))
        return 2;
    if (Y<long>::v != c)
        return 3;
    return 0;
}
