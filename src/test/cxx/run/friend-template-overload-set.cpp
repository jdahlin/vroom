/* [temp.friend]/9, [temp.over]: a friend function template that an
   enclosing template's parameter constrains is one more template of its
   name; the template declared before it stays callable */
template <class T, class U> concept Same = sizeof(T) == sizeof(U);

template <class L, class R> bool operator==(const L &a, const R &b)
{
    return a.v == b.v;
}

struct A {
    int v;
};

template <class T> struct W {
    T v;
    template <Same<T> U>
    friend bool operator==(const W &a, const W<U> &b)
    {
        return a.v == b.v;
    }
};

int main()
{
    W<int> w{1};
    A x{3}, y{3}, z{4};
    if (w.v != 1)
        return 3;
    if (!(x == y))
        return 1;
    if (x == z)
        return 2;
    return 0;
}
