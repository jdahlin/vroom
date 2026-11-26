// [over.inc], [over.match.oper]/2: postfix ++ and -- function templates
// (such as OpenTTD's enum operator--(enum_type &, int)) deduce against the
// dummy int(0) argument.

template <class E> struct is_inc {
    static constexpr bool value = false;
};

template <class E, bool = is_inc<E>::value>
struct enable_inc {};

template <class E>
struct enable_inc<E, true> {
    using type = bool;
};

template <class E, typename enable_inc<E>::type = true>
inline constexpr E &operator--(E &e)
{
    e = static_cast<E>(static_cast<unsigned char>(e) - 1);
    return e;
}

template <class E, typename enable_inc<E>::type = true>
inline constexpr E operator--(E &e, int)
{
    E old = e;
    --e;
    return old;
}

enum Dir : unsigned char { D0 = 0, D1 = 1, D2 = 2, D3 = 3 };
template <> struct is_inc<Dir> {
    static constexpr bool value = true;
};

int main()
{
    Dir d = D3;
    int steps = 0;
    for (; d > D0; d--)
        steps++;
    if (steps != 3 || d != D0)
        return 1;
    return 0;
}
