/* Partial specializations of member class templates ([temp.class.spec]/5):
   declared in the class or class template, chosen on use through the class
   qualifier and from a member function, with the enclosing class's
   arguments substituted (libc++'s ratio_multiply::__mul,
   __chrono/duration.h). */
struct plain {
    template <class T, bool B>
    struct X {
        static const int v = 1;
    };
    template <class T>
    struct X<T, true> {
        static const int v = 2;
    };
    template <class T>
    struct X<T *, false> {
        static const int v = 3;
    };
    static int f()
    {
        return X<int, true>::v * 100 + X<int, false>::v * 10 +
               X<int *, false>::v;
    }
};

template <class U>
struct outer {
    template <class T, bool B>
    struct X {
        static const int v = sizeof(U);
    };
    template <class T>
    struct X<T, true> {
        static const int v = 2 + sizeof(U);
    };
    template <class T>
    struct X<T *, false> {
        static const int v = 10 + sizeof(U);
    };
    static int f()
    {
        return X<int, true>::v * 100 + X<int, false>::v * 10 +
               X<int *, false>::v;
    }
};

static_assert(plain::X<int, true>::v == 2);
static_assert(plain::X<char, false>::v == 1);
static_assert(plain::X<char *, false>::v == 3);
static_assert(outer<char>::X<int, true>::v == 3);
static_assert(outer<int>::X<int, true>::v == 6);
static_assert(outer<int>::X<int, false>::v == 4);
static_assert(outer<short>::X<int *, false>::v == 12);

int main()
{
    if (plain::f() != 213)
        return 1;
    if (outer<char>::f() != 3 * 100 + 10 + 11)
        return 2;
    return outer<int>::f() == 6 * 100 + 40 + 14 ? 0 : 3;
}
