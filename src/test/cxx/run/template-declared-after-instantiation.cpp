/* [temp.inst]/2: a function template whose declarator instantiates a class
   with member templates is still instantiated with its own parameters */
extern "C" int printf(const char *, ...);

template <class T> struct holder {
    static const int value = 1;
    template <class U> int a(U)
    {
        return 1;
    }
    template <class U, class V> int b(U, V)
    {
        return 2;
    }
};

template <class A, class B, class C>
decltype(holder<int>{}.a(0)) sum(A a, B b, C c, int more = 1)
{
    return a + b + c + more;
}

template <class T> struct wrap {
    static const int value = sizeof(T);
};

int main()
{
    printf("%d %d\n", sum(1, 2, 3), sum(1, 2, 3, wrap<int>::value));
    return 0;
}
