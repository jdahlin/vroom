/* [temp.inst]/4, [basic.def.odr]: a recursive member template that only
   an unused constructor calls is not used by calling itself: what its body
   would instantiate (a deleted constructor's use) is not an error of the
   program (libc++'s __tree::__construct_from_tree) */
struct pinned {
    int v;
    pinned(int a) : v(a) {}
    pinned(const pinned &o) : v(o.v) {}
    pinned(pinned &&) = delete;
};
inline void *operator new(unsigned long, void *p, int) { return p; }

template <class T> void construct(void *at, T &&x)
{
    ::new (at, 0) T(static_cast<T &&>(x));
}

template <class T> struct holder {
    T value;
    holder(const T &t) : value(t) {}
    /* used by this constructor alone, which nothing uses */
    holder(holder &&o, int);
    template <class U> void walk(int n, U &x)
    {
        if (n) {
            walk(n - 1, x);
            put(static_cast<U &&>(x));
        }
    }
    template <class U> void put(U &&u)
    {
        alignas(T) unsigned char b[sizeof(T)];
        construct(b, static_cast<U &&>(u));
    }
};

template <class T> holder<T>::holder(holder &&o, int) : value(o.value)
{
    walk(1, value);
}

int main()
{
    holder<pinned> h(pinned(3));
    return h.value.v - 3;
}
