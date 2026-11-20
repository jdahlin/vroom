/* [ext:clang_type_traits] __is_destructible(T) and
   __is_nothrow_destructible(T) ([meta.unary.prop]): libc++'s
   <__type_traits/is_destructible.h> uses them when __has_builtin says they
   exist. */
struct Plain {};
struct Throws {
    ~Throws() noexcept(false) {}
};
struct Deleted {
    ~Deleted() = delete;
};
struct Quiet {
    ~Quiet() {}
};
class Hidden {
    ~Hidden() {}
};

static_assert(__is_destructible(int));
static_assert(__is_destructible(int &));
static_assert(__is_destructible(int[3]));
static_assert(!__is_destructible(int[]));
static_assert(!__is_destructible(void));
static_assert(!__is_destructible(void()));
static_assert(__is_destructible(Plain));
static_assert(__is_destructible(Throws));
static_assert(__is_destructible(Quiet));
static_assert(!__is_destructible(Deleted));
static_assert(!__is_destructible(Hidden));
static_assert(__is_nothrow_destructible(int));
static_assert(__is_nothrow_destructible(Plain));
static_assert(__is_nothrow_destructible(Quiet));
static_assert(!__is_nothrow_destructible(Throws));
static_assert(!__is_nothrow_destructible(Deleted));
static_assert(!__is_nothrow_destructible(void));
#if __has_builtin(__is_destructible) && __has_builtin(__is_nothrow_destructible)
static_assert(true);
#else
static_assert(false);
#endif

int main()
{
    return 0;
}
