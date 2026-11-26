// [temp.names]/5: a typedef inherited from a non-dependent specialization of a
// class template is not a dependent type, so a member template of it needs no
// 'template' keyword.

template <typename T> struct Pool {
    template <typename U> struct Wrapper {
        U value;
    };
    struct Item {
        typedef Pool<T> Pool;
    };
};

struct Base : Pool<int>::Item {};

template <typename T> struct Derived : Base {
    static Pool::Wrapper<T> make(T v) { return Pool::Wrapper<T>{v}; }
};

int main()
{
    auto w = Derived<int>::make(42);
    if (w.value != 42)
        return 1;
    return 0;
}
