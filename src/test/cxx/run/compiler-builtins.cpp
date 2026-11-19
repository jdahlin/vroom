/* [ext:gnu_library_builtins] builtins of vcc's C front end with scalar
   signatures, declared at their first use as clang does: libc++'s
   __libcpp_unreachable (__builtin_unreachable) and
   __cxx_atomic_thread_fence (__c11_atomic_thread_fence) */
[[noreturn]] inline void unreachable() { __builtin_unreachable(); }
inline void fence(int order) { __c11_atomic_thread_fence(order); }
int pick(int v)
{
    if (v == 1)
        return 40;
    if (v == 2)
        return 2;
    unreachable();
}
int main()
{
    fence(5);
    __c11_atomic_signal_fence(2);
    return pick(1) + pick(2) + __builtin_popcount(0) - 42;
}
