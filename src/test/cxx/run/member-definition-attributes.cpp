/* [temp.mem.func]/1: a member function of a class template defined outside
   it, with attributes before its declaration ([ext:gnu_attributes], libc++'s
   _LIBCPP_HIDE_FROM_ABI, [[nodiscard]]) */
template <class M> struct Lock {
    M *m;
    bool owns;
    __attribute__((__visibility__("hidden"))) __attribute__((__abi_tag__("x"))) void lock();
    [[nodiscard]] bool try_lock();
};
template <class M>
__attribute__((__visibility__("hidden"))) __attribute__((__abi_tag__("x"))) void
Lock<M>::lock()
{
    m->lock();
    owns = true;
}
template <class M> [[nodiscard]] bool Lock<M>::try_lock()
{
    owns = m->try_lock();
    return owns;
}
struct Mutex {
    int n = 0;
    void lock() { n += 40; }
    bool try_lock() { n += 2; return true; }
};
int main()
{
    Mutex mu;
    Lock<Mutex> l{&mu, false};
    l.lock();
    bool ok = l.try_lock();
    return mu.n - 42 + !ok + !l.owns;
}
