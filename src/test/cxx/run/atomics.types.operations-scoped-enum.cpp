/* [atomics.types.operations]/6-23, [ext:clang_atomic]: the __c11_atomic
   builtins that only move values take an _Atomic scoped enumeration, as
   std::atomic<E> of libc++ does */
enum class S : unsigned char { a, b, c };
static _Atomic(S) v;
int main()
{
    __c11_atomic_init(&v, S::a);
    __c11_atomic_store(&v, S::b, 5);
    if (__c11_atomic_load(&v, 5) != S::b)
        return 1;
    if (__c11_atomic_exchange(&v, S::c, 5) != S::b)
        return 2;
    S expected = S::c;
    if (!__c11_atomic_compare_exchange_strong(&v, &expected, S::a, 5, 5))
        return 3;
    return __c11_atomic_load(&v, 5) != S::a;
}
