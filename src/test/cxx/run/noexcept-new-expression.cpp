/* cxx-run-flags: -fexceptions */
/* [except.spec]/6: a new-expression is potentially-throwing if its allocation
   function or its initialization is (an allocator's
   noexcept(noexcept(::new (p) T(args...)))); a throw-expression is */
inline void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }

struct quiet {
    int v;
    quiet(int x) noexcept : v(x) {}
    quiet(const quiet &o) noexcept : v(o.v) {}
};
struct loud {
    int v;
    loud(int x) : v(x) {}
};

template <class T> struct allocator {
    template <class... Args>
    void construct(T *p, const Args &...args) noexcept(
        noexcept(::new (static_cast<void *>(p)) T(args...)))
    {
        ::new (static_cast<void *>(p)) T(args...);
    }
};

int main()
{
    alignas(8) unsigned char storage[8];
    void *p = storage;
    quiet *q = nullptr;
    loud *l = nullptr;
    int *i = nullptr;
    static_assert(noexcept(::new (p) quiet(1)), "");
    static_assert(!noexcept(::new (p) loud(1)), "");
    static_assert(noexcept(::new (p) int(1)), "");
    static_assert(noexcept(::new (p) int), "");
    static_assert(!noexcept(new int(1)), "");
    static_assert(!noexcept(new quiet(1)), "");
    static_assert(!noexcept(throw 1), "");
    allocator<quiet> a;
    allocator<loud> b;
    allocator<int> c;
    static_assert(noexcept(a.construct(q, 1)), "");
    static_assert(!noexcept(b.construct(l, 1)), "");
    static_assert(noexcept(c.construct(i, 1)), "");
    a.construct(static_cast<quiet *>(p), 4);
    return static_cast<quiet *>(p)->v - 4;
}
