// [dcl.type.elab]: elaborated-type-specifier with an unqualified
// simple-template-id (e.g. `Wrap<struct Box<T>>` and `struct Box<long>`).

template <class T> struct Box {
    T value;
};

template <class U> struct Wrap {
    using inner = U;
};

template <class T> struct Holder {
    using type = Wrap<struct Box<T>>::inner;
};

int main()
{
    Holder<int>::type b{42};
    if (b.value != 42)
        return 1;
    struct Box<long> c{99};
    if (c.value != 99)
        return 2;
    return 0;
}
