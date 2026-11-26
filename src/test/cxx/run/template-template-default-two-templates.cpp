/* [temp.param]/17: a template-id naming the argument of a template
   template-parameter takes the parameter's default arguments, though a
   parameter of the same name in another template (here a partial
   specialization) is bound to the same argument in between. */

template <class T, class S> struct serializer;

template <template <class T, class S = void> class J> struct holder {
    template <class V> static int get(V v) { return J<V>::read(v); }
};

template <class T> struct size_of {
    static constexpr int value = 0;
};

template <template <class, class = void> class J> struct size_of<holder<J>> {
    static constexpr int value = 2;
};

template <class T, class S> struct serializer {
    static int read(T v) { return static_cast<int>(v) + 1; }
};

int main()
{
    holder<serializer> h;
    (void)h;
    if (size_of<holder<serializer>>::value != 2)
        return 1;
    return holder<serializer>::get(10) == 11 ? 0 : 2;
}
