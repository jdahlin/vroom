/* [temp.names]/4, [temp.dep.type]/9: typename T::template rebind<U>::other
   in a template, a member template of a dependent type specialized */
struct alloc {
    template <class U> struct rebind {
        using other = U *;
    };
};

template <class T, class U> struct traits {
    using type = typename T::template rebind<U>::other;
};

template <class T> int size_of_rebound()
{
    typename traits<T, short>::type p = 0;
    return sizeof(typename T::template rebind<char>::other) + sizeof(*p);
}

int main()
{
    traits<alloc, int>::type p = 0;
    return size_of_rebound<alloc>() == sizeof(char *) + sizeof(short) && !p
               ? 0
               : 1;
}
