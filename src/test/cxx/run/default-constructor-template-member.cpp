/* [class.default.ctor]/2.1, [temp.mem]/1: a member whose default constructor
   is a constructor template with a default template argument (libc++'s
   unique_ptr) does not delete the enclosing class's implicit default
   constructor. */
template <class D = int>
struct pointer_like {
    template <class E = D>
    pointer_like() : p(7) {}
    pointer_like(const pointer_like &) = delete;
    int p;
};

template <class T>
struct holder {
    pointer_like<T> m;
};

struct outer {
    holder<int> h;
};

int main()
{
    outer o;
    return o.h.m.p - 7;
}
