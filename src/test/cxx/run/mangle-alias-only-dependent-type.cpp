/* [temp.alias]/2: void_t<T> is void, so a non-type template parameter's type
   that depends on parameters only through it is not dependent: its mangled
   type is the specialization's (libc++'s operator<< for unique_ptr) */

template <class T> T &&declval();
template <class... T> using void_t = void;
template <class A, class B> struct is_same {
    static constexpr bool value = false;
};
template <class A> struct is_same<A, A> {
    static constexpr bool value = true;
};
template <bool B, class T = void> struct enable_if {
};
template <class T> struct enable_if<true, T> {
    using type = T;
};
template <bool B, class T> using enable_if_t = typename enable_if<B, T>::type;

struct sink {
    int got;
    sink &operator<<(int *p)
    {
        got = *p;
        return *this;
    }
};
template <class T> struct owner {
    using pointer = T *;
    pointer p;
};
template <class Y> struct box {
};

template <class Y,
          enable_if_t<is_same<void, void_t<decltype(declval<sink &>() << declval<
                                                    typename owner<Y>::pointer>())>>::value,
                      int> = 0>
sink &operator<<(sink &s, owner<Y> const &o)
{
    return s << o.p;
}

template <class Y, enable_if_t<is_same<void, void_t<Y>>::value, int> = 0>
int plain(box<Y>)
{
    return 1;
}

template <class Y,
          enable_if_t<is_same<box<void_t<Y>>, box<void>>::value, long> = 2>
int nested(box<Y>)
{
    return 2;
}

int main()
{
    sink s = {0};
    int seven = 7;
    owner<int> o = {&seven};
    s << o;
    return s.got + plain(box<int>()) + nested(box<char>()) == 10 ? 0 : 1;
}
