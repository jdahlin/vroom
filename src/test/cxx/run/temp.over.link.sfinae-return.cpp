// [temp.over.link]: two function template definitions with the same parameter
// list and different SFINAE-dependent return types (e.g. Catch2's
// `convertUnstreamable`) are distinct overloads, not redefinitions.

template <bool B, class T = void> struct enable_if {};
template <class T> struct enable_if<true, T> {
    using type = T;
};

template <class T> struct is_big {
    static constexpr bool value = sizeof(T) >= 4;
};

template <class T>
typename enable_if<!is_big<T>::value, int>::type classify(const T &)
{
    return 1;
}

template <class T>
typename enable_if<is_big<T>::value, int>::type classify(const T &)
{
    return 2;
}

int main()
{
    char c = 0;
    int i = 0;
    if (classify(c) != 1 || classify(i) != 2)
        return 1;
    return 0;
}
