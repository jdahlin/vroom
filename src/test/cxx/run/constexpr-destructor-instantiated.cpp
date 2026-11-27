/* cxx-run-flags: -std=c++20 */
/* [dcl.constexpr]/6: the instantiation's destructor is not constexpr,
   and no error, when a subobject's implicit one is not */
extern "C" int printf(const char *, ...);

static int destroyed;
struct counted {
    ~counted() { destroyed++; }
};
struct wraps {
    counted c;
};

template <class T> struct holder {
    union {
        T value;
        char none;
    };
    bool engaged = false;
    constexpr holder() : none() {}
    constexpr ~holder()
    {
        if (engaged)
            value.~T();
    }
    void emplace()
    {
        new (&value) T();
        engaged = true;
    }
};
void *operator new(decltype(sizeof 0), void *p) noexcept;
void *operator new(decltype(sizeof 0), void *p) noexcept { return p; }

int main()
{
    {
        holder<wraps> h;
        h.emplace();
    }
    printf("%d\n", destroyed);
    return destroyed != 1;
}
