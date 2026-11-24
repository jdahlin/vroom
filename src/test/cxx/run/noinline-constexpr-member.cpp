/* [ext:gnu_attributes] noinline on a member function template of a class
   template defined outside the class, constexpr */
template <class T> struct holder {
    T v;
    constexpr void set(T x);
};

template <class T>
constexpr __attribute__((__noinline__)) void holder<T>::set(T x)
{
    v = x;
}

int main(int argc, char **)
{
    holder<int> h = {0};
    h.set(argc + 4);
    if (h.v != argc + 4)
        return 1;
    return 0;
}
