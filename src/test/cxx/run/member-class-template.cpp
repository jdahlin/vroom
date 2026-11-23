/* Member class templates ([temp.mem]/1): a class template declared in a
   class or in a class template, its specializations instantiated with the
   enclosing class's arguments already substituted, as libc++'s
   duration::__no_overflow (__chrono/duration.h). */
struct plain {
    static const int base = 10;
    template <class R>
    struct size_of {
        static const int value = sizeof(R) + base;
    };
};

template <class A>
struct outer {
    static const int scale = sizeof(A);
    template <class R1, class R2>
    struct pair {
        static const int value = sizeof(R1) * scale + sizeof(R2);
        A first;
        R1 second;
        int sum() const
        {
            return (int)first + (int)second + value;
        }
    };
};

static_assert(plain::size_of<short>::value == 12);
static_assert(outer<int>::pair<char, short>::value == 6);
static_assert(outer<long>::pair<int, char>::value == 33);

int main()
{
    outer<int>::pair<char, short> p{3, 4};
    if (p.sum() != 13)
        return 1;
    plain::size_of<char> s;
    (void)s;
    return outer<short>::pair<long, long>::value == 24 ? 0 : 2;
}
