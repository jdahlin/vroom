/* [ext:gnu_library_builtins] the generic __atomic_load, __atomic_store,
   __atomic_exchange and __atomic_compare_exchange on any trivially copyable
   type (GCC manual 6.55: "the generic versions work on objects of any
   size"), and the _n forms on enumerations, bool and pointers; the size
   of the type decides between an instruction and the libatomic call. */
enum E { A, B, C };
struct alignas(8) Small {
    int a, b;
};

int main()
{
    E e = B;
    if (__atomic_load_n(&e, 5) != B)
        return 1;
    __atomic_store_n(&e, C, 5);
    if (e != C)
        return 2;
    E old = __atomic_exchange_n(&e, A, 5);
    if (old != C || e != A)
        return 3;

    Small s = {1, 2}, t = {3, 4}, r = {0, 0};
    __atomic_load(&s, &r, 5);
    if (r.a != 1 || r.b != 2)
        return 5;
    __atomic_store(&s, &t, 5);
    if (s.a != 3 || s.b != 4)
        return 6;
    __atomic_exchange(&s, &r, &t, 5);
    if (t.a != 3 || s.a != 1)
        return 7;
    Small expect = {1, 2}, want = {9, 9};
    if (!__atomic_compare_exchange(&s, &expect, &want, false, 5, 5) ||
        s.a != 9)
        return 8;
    bool flag = false;
    __atomic_store_n(&flag, true, 5);
    if (!__atomic_load_n(&flag, 5))
        return 11;
    double d = 1.5, dr;
    __atomic_load(&d, &dr, 5);
    if (dr != 1.5)
        return 12;
    return 0;
}
