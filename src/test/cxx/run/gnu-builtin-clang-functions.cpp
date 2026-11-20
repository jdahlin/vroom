/* [ext:clang_builtin_functions] clang's builtins that libc++ calls and
   that no program here executes: __builtin_verbose_trap(category, message)
   (the two arguments are string literals), __builtin_assume_dereferenceable
   (pointer, size), __builtin_is_virtual_base_of(Base, Derived) and
   __builtin_clear_padding (pointer to a trivially copyable class). */
typedef unsigned long size_t;

struct B {};
struct D : virtual B {};
struct P {
    char c;
    int i;
};

static_assert(__builtin_is_virtual_base_of(B, D));
static_assert(!__builtin_is_virtual_base_of(D, B));
static_assert(!__builtin_is_virtual_base_of(B, B));

[[noreturn]] static void fail()
{
    __builtin_verbose_trap("category", "message");
}

static int dereferenceable(int *p)
{
    __builtin_assume_dereferenceable(p, sizeof(int));
    return *p;
}

static void clear(P *p)
{
    __builtin_clear_padding(p);
}

int main(int argc, char **)
{
    if (argc > 5)
        fail();
    int x = 4;
    if (dereferenceable(&x) != 4)
        return 1;
    P p;
    __builtin_memset(&p, 0xff, sizeof p);
    p.c = 1;
    p.i = 2;
    clear(&p);
    unsigned char *bytes = (unsigned char *)&p;
    if (bytes[0] != 1 || bytes[1] != 0 || bytes[2] != 0 || bytes[3] != 0)
        return 2;
    return 0;
}
