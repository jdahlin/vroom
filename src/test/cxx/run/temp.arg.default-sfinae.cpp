// [temp.deduct]/8: an ill-formed default template argument in a class/alias
// template specialization during template argument substitution is a SFINAE
// failure, not a hard error (e.g. fmt::detail::char_t<T>).

template <class T, class = typename T::value_type> struct CharOf {
    using type = typename T::value_type;
};

template <class T> using char_t = typename CharOf<T>::type;

struct HasChar {
    using value_type = char;
};

template <class T, class C = char_t<T>> int map(const T &)
{
    return 1;
}

int map(int) { return 2; }

int main()
{
    HasChar hc;
    if (map(hc) != 1)
        return 1;
    if (map(42) != 2)
        return 2;
    return 0;
}
