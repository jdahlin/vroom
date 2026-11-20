/* [temp.inst]/4: an out-of-class definition of a class template's member
   that repeats libc++'s _LIBCPP_HIDE_FROM_ABI attributes, including
   exclude_from_explicit_instantiation, of a specialization that is used */
template <class M> class lock_guard_like {
    M *m_;
    bool owns_;

public:
    explicit lock_guard_like(M &m) : m_(&m), owns_(false) {}
    __attribute__((__visibility__("hidden")))
    __attribute__((__exclude_from_explicit_instantiation__)) void lock();
};
template <class M>
__attribute__((__visibility__("hidden")))
__attribute__((__exclude_from_explicit_instantiation__)) void
lock_guard_like<M>::lock()
{
    m_->lock();
    owns_ = true;
}
struct mutex_like {
    int locked = 0;
    void lock() { locked++; }
};
int main()
{
    mutex_like m;
    lock_guard_like<mutex_like> g(m);
    g.lock();
    return m.locked - 1;
}
