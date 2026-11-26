/* [abi:expressions]: a conditional-expression in a non-type template
   parameter's type, qu */

template <bool B, class T = void> struct enable_if {
};
template <class T> struct enable_if<true, T> {
    using type = T;
};
template <class T> struct is_small {
    static constexpr bool value = sizeof(T) == 1;
};
template <class T> struct yes {
    static constexpr bool value = true;
};

template <class T,
          typename enable_if<is_small<T>::value ? yes<T>::value
                                                : yes<T *>::value,
                             int>::type = 0>
int pick(T v)
{
    return static_cast<int>(v) + 1;
}

int main()
{
    return pick('a') + pick(2L) == 'a' + 4 ? 0 : 1;
}
