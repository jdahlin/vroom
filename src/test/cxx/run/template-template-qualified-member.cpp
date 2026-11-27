// Qualified template template arguments naming member alias and class
// templates, with and without the disambiguating `template` keyword
// ([temp.arg.template]/1).

template <class T>
struct Wrap {
    using value_type = T;
};

template <class T>
struct Xref {
    template <class U>
    using apply = Wrap<U>;

    template <class U>
    struct Box {
        U value;
    };
};

template <class T, template <class> class F>
struct ApplyAlias {
    using type = F<T>;
};

template <class T, template <class> class F>
using ApplyAliasT = typename ApplyAlias<T, F>::type;

template <class T, class U>
using DependentApplyT =
    typename ApplyAlias<U, Xref<T>::template apply>::type;

template <class T, class U>
using DependentBoxT =
    typename ApplyAlias<U, Xref<T>::template Box>::type;

struct PlainOwner {
    template <class U>
    using Id = U;

    template <class U>
    struct Pair {
        U a;
        U b;
    };
};

int main()
{
    DependentApplyT<int, long> w{};
    if (sizeof(w) != 1)
        return 1;

    DependentBoxT<int, int> b{42};
    if (b.value != 42)
        return 2;

    ApplyAliasT<int, Xref<double>::template apply> w2{};
    if (sizeof(w2) != 1)
        return 3;

    ApplyAliasT<int, PlainOwner::Id> x = 7;
    if (x != 7)
        return 4;

    ApplyAliasT<int, PlainOwner::Pair> p{10, 20};
    if (p.a + p.b != 30)
        return 5;

    return 0;
}
