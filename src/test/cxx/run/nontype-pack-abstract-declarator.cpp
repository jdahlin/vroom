// std: c++20
/* [dcl.decl.general]/1, [temp.param]/17: an unnamed non-type template
   parameter pack whose abstract declarator ends in "..." (int&..., int*...),
   as libc++'s __check_pair_construction has */
template <int &...>
constexpr bool refs()
{
    return true;
}

template <int *...>
constexpr bool pointers()
{
    return true;
}

struct check {
    template <int &...>
    static constexpr int enable()
    {
        return 3;
    }
};

int main()
{
    static_assert(refs<>() && pointers<>());
    if (sizeof(check) == 0)
        return 2;
    return 0;
}
