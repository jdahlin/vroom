// [class.mfct]/1, [temp.mem.func]/1, [class.ctor]/1, [class.dtor]/1: the
// constructors and destructor of a class template defined outside it (libc++'s
// list, forward_list and __hash_table), with and without inline and with a
// ctor-initializer; and inline on those of an ordinary class.
static int log_;

struct Plain {
    int v;
    Plain(int a);
    Plain(const Plain &o);
    ~Plain();
};
inline Plain::Plain(int a) : v(a) { log_ += 1; }
Plain::Plain(const Plain &o) : v(o.v + 1) { log_ += 10; }
inline Plain::~Plain() { log_ += 100; }

template <class T> struct Box {
    T v;
    Box(int a);
    Box(Box &&o) noexcept;
    ~Box();
    int get() const { return static_cast<int>(v); }
};
template <class T> inline Box<T>::Box(int a) : v(static_cast<T>(a)) { log_ += 1000; }
template <class T> Box<T>::Box(Box &&o) noexcept : v(o.v + 1) { log_ += 10000; }
template <class T> Box<T>::~Box() { log_ += 100000; }

int main()
{
    {
        Plain p(1);
        Plain q(p);
        if (q.v != 2)
            return 1;
    }
    if (log_ != 211)
        return 2;
    log_ = 0;
    {
        Box<long> b(5);
        Box<long> c(static_cast<Box<long> &&>(b));
        if (c.get() != 6)
            return 3;
    }
    if (log_ != 211000)
        return 4;
    return 0;
}
