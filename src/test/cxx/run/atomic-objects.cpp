/* [ext:clang_atomic] atomic objects in C++ (clang's C11 _Atomic): both
   spellings; read where not the operand of &, sizeof or an assignment's
   left operand (one atomic load), assigned, compound-assigned, ++ and --
   (atomic store, read-modify-write), initialized as the value type, and
   the __c11_atomic builtins on a pointer to one, as <stdatomic.h>'s macros
   (atomic_load_explicit(p, mo) is __c11_atomic_load(p, mo)) pass it; the
   GNU __atomic builtins take a plain object */
typedef unsigned long size_t;
enum colour { red, green, blue };
static _Atomic int counter = 3;
static _Atomic(const char *) text = "abc";
static _Atomic unsigned char *bytes;
static _Atomic unsigned char storage[4];
static long plain;
static _Atomic(colour) shade;
struct table {
    _Atomic size_t used;
    _Atomic(table *) next;
};
static table head;
static int sum(int a, int b) { return a + b; }
int main()
{
    counter += 4;
    int before = counter++;
    int after = ++counter;
    counter -= 1;
    text++;
    storage[2] = 1;
    bytes = storage;
    __c11_atomic_store(&bytes[1], 9, __ATOMIC_RELEASE);
    __c11_atomic_fetch_add(bytes + 2, 4, __ATOMIC_RELAXED);
    __atomic_fetch_add(&plain, 2, __ATOMIC_RELAXED);
    shade = blue;
    head.used = 2;
    head.next = &head;
    head.next->used += 1;
    int expected = 9;
    _Atomic int *p = &counter;
    __c11_atomic_compare_exchange_strong(p, &expected, 20, __ATOMIC_SEQ_CST,
                                         __ATOMIC_SEQ_CST);
    if (sizeof counter != 4 || sizeof(_Atomic(char)) != 1)
        return 1;
    return sum(counter, before) + after + *text + storage[1] + storage[2] +
           (int)shade + (int)head.used + (int)__atomic_load_n(&plain, 2) -
           20 - 7 - 9 - 'b' - 9 - 5 - 2 - 3 - 2;
}
