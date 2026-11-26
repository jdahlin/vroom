// [class.derived]: base-specifier using nested-name-specifier template
// simple-template-id (e.g. StrongType::Typedef mixin pack expansion).

struct AddOne {
    template <typename Derived, typename Base>
    struct mixin {
        constexpr Base inc() const
        {
            return static_cast<const Derived *>(this)->value + 1;
        }
    };
};

struct DoubleIt {
    template <typename Derived, typename Base>
    struct mixin {
        constexpr Base dbl() const
        {
            return static_cast<const Derived *>(this)->value * 2;
        }
    };
};

template <typename TBase, typename... TProps>
struct Strong : public TProps::template mixin<Strong<TBase, TProps...>, TBase>... {
    TBase value;
    constexpr explicit Strong(TBase v) : value(v) {}
};

int main()
{
    Strong<int, AddOne, DoubleIt> s(21);
    if (s.inc() != 22)
        return 1;
    if (s.dbl() != 42)
        return 2;
    return 0;
}
