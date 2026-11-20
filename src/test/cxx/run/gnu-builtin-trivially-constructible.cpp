/* [ext:clang_type_traits] __is_trivially_constructible(T, Args...)
   ([meta.unary.prop]): libc++'s <__type_traits/is_trivially_constructible.h>
   and <optional>'s storage choose by it. */
struct Plain {
    int a;
};
struct User {
    User() {}
    User(const User &) {}
};
struct Copy {
    Copy() = default;
    Copy(const Copy &) = default;
    int v = 0;
};
struct Virt {
    virtual void f() {}
};

static_assert(__is_trivially_constructible(int));
static_assert(__is_trivially_constructible(int, int));
static_assert(__is_trivially_constructible(int, long));
static_assert(__is_trivially_constructible(int *, int *));
static_assert(!__is_trivially_constructible(int, int *));
static_assert(__is_trivially_constructible(Plain));
static_assert(__is_trivially_constructible(Plain, const Plain &));
static_assert(__is_trivially_constructible(Plain, Plain &&));
static_assert(!__is_trivially_constructible(Plain, int, int));
static_assert(!__is_trivially_constructible(User));
static_assert(!__is_trivially_constructible(User, const User &));
static_assert(!__is_trivially_constructible(Copy));
static_assert(__is_trivially_constructible(Copy, const Copy &));
static_assert(!__is_trivially_constructible(Virt));
static_assert(!__is_trivially_constructible(Virt, const Virt &));
static_assert(__is_trivially_constructible(int &, int &));
static_assert(!__is_trivially_constructible(int &, long &));

template <class T>
inline constexpr bool movable = __is_trivially_constructible(T, T &&);
static_assert(movable<int>);
static_assert(movable<Plain>);
static_assert(!movable<User>);

int main()
{
    return 0;
}
