/* [ext:clang_atomic] _Atomic(T) as a type-specifier: C11's layout as
   clang gives it (a T of up to 16 bytes widened to a power of two and
   aligned to it), in a class template as libc++'s c11.h declares it. */
struct three {
    char c[3];
};
struct big {
    char c[24];
};

template <class T> struct atomic_base {
    _Atomic(T) value;
    char after;
};

static_assert(sizeof(_Atomic(char)) == 1, "");
static_assert(sizeof(_Atomic(three)) == 4 && alignof(_Atomic(three)) == 4, "");
static_assert(sizeof(_Atomic(big)) == 24 && alignof(_Atomic(big)) == 1, "");
static_assert(sizeof(atomic_base<long>) == 16, "");
static_assert(sizeof(atomic_base<three>) == 8, "");

struct holder {
    char tag;
    atomic_base<int> a;
};

int main()
{
    holder h;
    h.tag = 1;
    h.a.after = 2;
    if (sizeof h != 12 || (char *)&h.a.after - (char *)&h != 8)
        return 1;
    return h.tag + h.a.after == 3 ? 0 : 2;
}
