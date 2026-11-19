/* [ext:gnu_offsetof] __builtin_offsetof: a constant of type std::size_t,
   through members, nested members and array elements; libc++'s
   __datasizeof_v applies it in a variable template. */
typedef __SIZE_TYPE__ size_t;

struct inner {
    char c;
    int a[4];
};
struct outer {
    char tag;
    double d;
    inner in[3];
};

template <class T> struct first_padding {
    T v;
    char padding;
};
template <class T>
inline const size_t data_size = __builtin_offsetof(first_padding<T>, padding);

static_assert(__builtin_offsetof(outer, d) == 8, "");
static_assert(__builtin_offsetof(outer, in[2].a[1]) == 16 + 2 * 20 + 8, "");

int main()
{
    size_t x = __builtin_offsetof(inner, a[3]);
    if (x != 16)
        return 1;
    if (__builtin_offsetof(outer, in[1].c) != 36)
        return 2;
    return 0;
}
