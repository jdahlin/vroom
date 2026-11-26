// [ext.gnu]: visibility("hidden") and exclude_from_explicit_instantiation
// (e.g. libc++ _LIBCPP_HIDE_FROM_ABI) on a static data member.

template <class State> struct Lock {
    __attribute__((__visibility__("hidden")))
    __attribute__((__exclude_from_explicit_instantiation__))
    static constexpr State mask = 4u;

    __attribute__((__visibility__("hidden")))
    __attribute__((__exclude_from_explicit_instantiation__))
    static inline State count = 1u;
};

int main()
{
    if (Lock<unsigned>::mask != 4u)
        return 1;
    Lock<unsigned>::count += Lock<unsigned>::mask;
    if (Lock<unsigned>::count != 5u)
        return 2;
    return 0;
}
