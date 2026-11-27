// A static inline data member declared as an array of unknown bound is
// completed by its in-class initializer ([dcl.array]/7), so its bound is
// known for sizeof and template argument deduction (e.g. std::span).

using size_t = decltype(sizeof(0));

template <class T>
struct Span {
    const T *ptr;
    size_t len;

    template <size_t N>
    Span(T (&arr)[N]) : ptr(arr), len(N) {}
};

struct Item {
    int value;
};

template <class TImpl>
struct Base {
    virtual ~Base() = default;
    virtual Span<const Item> Get() const
    {
        return static_cast<const TImpl *>(this)->items;
    }
};

struct Derived : Base<Derived> {
    static inline const Item items[] = {{10}, {20}, {30}};
};

struct MutableHolder {
    static inline int nums[] = {1, 2, 3, 4};
};

int main()
{
    if (sizeof(Derived::items) != 3 * sizeof(Item))
        return 1;
    if (sizeof(MutableHolder::nums) != 4 * sizeof(int))
        return 2;

    Span<const Item> direct = Derived::items;
    if (direct.len != 3 || direct.ptr[0].value != 10 ||
        direct.ptr[2].value != 30)
        return 3;

    Derived d;
    const Base<Derived> &b = d;
    Span<const Item> via_crtp = b.Get();
    if (via_crtp.len != 3 || via_crtp.ptr[1].value != 20)
        return 4;

    Span<int> m = MutableHolder::nums;
    if (m.len != 4 || m.ptr[3] != 4)
        return 5;

    return 0;
}
