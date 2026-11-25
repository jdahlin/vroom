template <class T>
struct outer {
    template <bool>
    struct inner;

    template <class U = T>
    static int make()
        requires (sizeof(U) > 0)
    {
        return inner<true>::value;
    }
};

template <class T>
template <bool B>
struct outer<T>::inner {
    static constexpr int value = B ? 42 : 0;
};

int main() {
    return outer<int>::make() == 42 ? 0 : 1;
}
