// [temp.mem]/1: a constructor template declared in a class (libc++'s
// _CmpUnspecifiedParam in <compare>): the class has a user-declared
// constructor, so it is no aggregate ([dcl.init.aggr]/1.1) and has no
// implicit default constructor ([class.default.ctor]/1); pointers and
// references to it, its size and its other members are usable.  A
// construction of it is not analysed yet (unsupported/constructor-template.cpp).
struct Zero {
    template <class T, class = int>
    __attribute__((__visibility__("hidden"))) consteval Zero(T zero) noexcept
    {
        (void)zero;
    }
    static int answer() { return 42; }
};

struct Holder {
    template <class T> explicit constexpr Holder(T *p) {}
    int value;
};

int read(const Holder *h) { return h ? h->value : -1; }
int through(Zero *z) { return z ? 1 : 2; }

int main()
{
    if (sizeof(Zero) != 1 || sizeof(Holder) != sizeof(int))
        return 1;
    if (Zero::answer() != 42)
        return 2;
    if (read(nullptr) != -1 || through(nullptr) != 2)
        return 3;
    return 0;
}
