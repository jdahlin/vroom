/* What libc++'s headers and the macOS SDK write:
   - [ext:clang_type_traits] __is_same(T, U) ([meta.rel] is_same), as
     __type_traits/is_same.h's base class, and GCC's __is_same_as;
   - [ext:gnu_attributes] always_inline and noinline on a member function
     (typeinfo's _LIBCPP_ALWAYS_INLINE static members);
   - an aligned attribute after a class-specifier's closing brace raises
     the class's alignment (mach/arm/_structs.h): sizeof and alignof are
     clang's */
#include <stdio.h>

template <bool B> struct bool_constant {
    static constexpr bool value = B;
};
template <class T, class U>
struct is_same : bool_constant<__is_same(T, U)> {};

static_assert(__is_same(int, int), "");
static_assert(!__is_same(int, const int), "");
static_assert(__is_same_as(long, long), "");

struct names {
    __attribute__((__always_inline__)) static const char *first() noexcept
    {
        return "first";
    }
    __attribute__((noinline)) int twice(int x) const { return 2 * x; }
};

struct sve_z {
    char z[16][256];
} __attribute__((aligned(4)));
struct small {
    char c;
} __attribute__((aligned(8))) one_small;
struct outer {
    struct inner {
        char c;
    } __attribute__((__aligned__(16))) i;
    char e;
};

int main()
{
    printf("%d %d %d %d\n", is_same<int, int>::value,
           is_same<int, long>::value, is_same<char *, char *>::value,
           is_same<const int, int>::value);
    names n;
    printf("%s %d\n", names::first(), n.twice(21));
    printf("%zu %zu %zu %zu %zu %zu\n", sizeof(sve_z), alignof(sve_z),
           sizeof(small), alignof(small), sizeof(outer), alignof(outer));
    one_small.c = 'x';
    return one_small.c != 'x';
}
