/* [temp.friend]/2: a default template argument of a friend template defined
   in a class template names the class's parameter (std::quoted's proxy) */
extern "C" int printf(const char *, ...);

template <class A, class B> struct same {
    static const bool value = false;
};
template <class A> struct same<A, A> {
    static const bool value = true;
};
template <bool, class T = void> struct en {
};
template <class T> struct en<true, T> {
    using type = T;
};
template <bool B, class T = void> using en_t = typename en<B, T>::type;

struct os {
};

template <class Tr> struct proxy {
    int v;
    template <class T2, en_t<same<Tr, void>::value || same<Tr, T2>::value,
                             int> = 0>
    friend int operator<<(T2 &, const proxy &p)
    {
        return p.v;
    }
};

int main()
{
    os o;
    proxy<os> p{3};
    proxy<void> q{4};
    printf("%d %d\n", o << p, o << q);
    return 0;
}
