#include <stdlib.h>
#include <stdatomic.h>
typedef _Atomic int atomic_alias;
_Atomic atomic_alias repeated;
extern _Atomic(int (*)(int(*)[], int(*)[10])) merged;
extern _Atomic(int (*)(int(*)[10], int(*)[])) merged;
extern _Atomic(int (*)(int(*)[10], int(*)[10])) merged;
struct S { int n; };
_Static_assert(!__builtin_types_compatible_p(_Atomic int, int), "distinct atomic type");
_Static_assert(__builtin_types_compatible_p(_Atomic atomic_alias, atomic_alias), "qualifier idempotence");
_Static_assert(ATOMIC_CHAR16_T_LOCK_FREE == __CLANG_ATOMIC_CHAR16_T_LOCK_FREE, "char16 lock freedom");
_Static_assert(ATOMIC_CHAR32_T_LOCK_FREE == __CLANG_ATOMIC_CHAR32_T_LOCK_FREE, "char32 lock freedom");
_Static_assert(ATOMIC_WCHAR_T_LOCK_FREE == __CLANG_ATOMIC_WCHAR_T_LOCK_FREE, "wchar lock freedom");
int main(void)
{
    _Atomic int x = 1;
    _Static_assert(__builtin_types_compatible_p(__typeof__(x = 2), int), "assignment value type");
    _Static_assert(__builtin_types_compatible_p(__typeof__(x += 2), int), "compound assignment value type");
    _Static_assert(__builtin_types_compatible_p(__typeof__(++x), int), "prefix value type");
    _Static_assert(__builtin_types_compatible_p(__typeof__(x++), int), "postfix value type");
    if ((x = 7) != 7 || (x += 3) != 10 || x != 10) abort();
    if (x++ != 10 || ++x != 12 || --x != 11 || x-- != 11 || x != 10) abort();
    struct S plain = { 42 };
    _Atomic struct S record = plain;
    _Static_assert(__builtin_types_compatible_p(__typeof__(record = plain), struct S), "record value type");
    struct S copy = record;
    if (copy.n != 42) abort();
    (void)sizeof(record.n);
    if (0) record.n = 4;
    return 0;
    record.n = 5;
}
