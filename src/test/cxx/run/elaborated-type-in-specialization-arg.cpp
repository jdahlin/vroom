/* [basic.scope.pdecl]/7.2: an elaborated-type-specifier in a template
   argument of an explicit or partial specialization declares the class in
   the enclosing namespace scope, not the specialization's template
   parameter scope. */

template <class T> struct Traits;

template <class T> struct Base {
    static constexpr int value = Traits<T>::value;
    int arr[value];
};

template <> struct Traits<struct Foo> {
    static constexpr int value = 7;
};

struct Foo : Base<Foo> {};

namespace ns {
template <class T> struct NsTraits;

template <class T> struct NsBase {
    static constexpr int value = NsTraits<T>::value;
};

template <> struct NsTraits<struct Bar> {
    static constexpr int value = 11;
};

struct Bar : NsBase<Bar> {};
} // namespace ns

template <class T, class U> struct PairTraits {
    static constexpr int value = 0;
};

template <class U> struct PairTraits<struct Baz, U> {
    static constexpr int value = 13;
};

struct Baz {};

int main()
{
    Foo f{};
    if (Foo::value != 7)
        return 1;
    if (sizeof(f.arr) / sizeof(f.arr[0]) != 7)
        return 2;
    if (ns::Bar::value != 11)
        return 3;
    if (PairTraits<Baz, int>::value != 13)
        return 4;
    return 0;
}
