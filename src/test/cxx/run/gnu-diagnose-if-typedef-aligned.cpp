/* clang's diagnose_if on a function (libc++'s _LIBCPP_CHECK_STORE_MEMORY_ORDER)
   and GNU aligned on a typedef-name equal to the type's own alignment
   (linux/types.h's __u128, __aligned_u64): accepted. */
typedef unsigned __int128 u128 __attribute__((aligned(16)));
typedef unsigned long long aligned_u64 __attribute__((aligned(8)));

struct S {
    aligned_u64 a;
    u128 b;
};

void store(int *p, int order)
    __attribute__((diagnose_if(order > 3, "bad order", "warning")));
void store(int *p, int order)
{
    *p = order;
}

int main()
{
    int x = 0;
    store(&x, 2);
    if (alignof(u128) != 16 || alignof(aligned_u64) != 8 || sizeof(S) != 32 ||
        x != 2)
        return 1;
    return 0;
}
