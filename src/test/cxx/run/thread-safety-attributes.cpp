/* clang's thread-safety attributes (libc++'s _LIBCPP_CAPABILITY,
   _LIBCPP_ACQUIRE_CAPABILITY, ...) are accepted and change nothing without
   -Wthread-safety, in both spellings, with string, member and this
   arguments */
struct [[_Clang::__capability__("mutex")]] M {
    int held = 0;
    void lock() __attribute__((__acquire_capability__)) { held++; }
    [[_Clang::__release_capability__]] void unlock() { held--; }
    [[_Clang::__try_acquire_capability__(true)]] bool try_lock()
    {
        held++;
        return true;
    }
    [[_Clang::__acquire_shared_capability__]] void lock_shared() { held += 2; }
    void unlock_shared() __attribute__((__release_shared_capability__))
    {
        held -= 2;
    }
    void touch() __attribute__((__requires_capability__(this))) { held += 10; }
    [[_Clang::__no_thread_safety_analysis__]] int raw() { return held; }
};
struct [[clang::scoped_lockable]] G {
    M &m;
    explicit G(M &mu) __attribute__((__acquire_capability__(mu))) : m(mu)
    {
        m.lock();
    }
    ~G() __attribute__((__release_capability__)) { m.unlock(); }
};
struct S {
    M mu;
    int v __attribute__((guarded_by(mu))) = 3;
    int f() __attribute__((requires_capability(mu)))
    __attribute__((__no_thread_safety_analysis__))
    {
        return v;
    }
};
int main()
{
    S s;
    {
        G g(s.mu);
        s.mu.touch();
    }
    s.mu.try_lock();
    s.mu.lock_shared();
    s.mu.unlock_shared();
    s.mu.unlock();
    return s.mu.raw() + s.f() - 13;
}
