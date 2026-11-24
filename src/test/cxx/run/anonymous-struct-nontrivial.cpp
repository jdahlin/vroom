// [class.union.anon]/1 with clang's extension of an unnamed struct: an
// anonymous struct member whose members have default member initializers
// and non-trivial special members (libc++'s _LIBCPP_COMPRESSED_PAIR).
template <class T> struct Pad { char c; };
static int destroyed;
struct Counted {
    int v;
    Counted() : v(5) {}
    ~Counted() { destroyed++; }
};
struct Holder {
    int* begin = nullptr;
    struct {
        [[no_unique_address]] int* cap = nullptr;
        [[no_unique_address]] Pad<int*> p1;
        [[no_unique_address]] Counted c;
        [[no_unique_address]] Pad<Counted> p2;
    };
    int total() const { return c.v + (cap == nullptr) + (begin == nullptr); }
};
static_assert(sizeof(Holder) == 32);

int main() {
    {
        Holder h;
        if (h.total() != 7)
            return 1;
        h.c.v = 9;
        if (h.total() != 11)
            return 2;
    }
    return destroyed != 1;
}
