/* [over.best.ics]/10: a best viable function that uses an ambiguous
   conversion sequence makes the call ill-formed, which a type trait or a
   substitution reports as false ([temp.deduct]/8) */
struct target {};
struct both {
    operator int() const;
    operator long() const;
};
struct takes {
    takes &operator=(float);
};

template <class T, class U> struct assignable {
    template <class A, class B>
    static char test(decltype(*static_cast<A *>(nullptr) = B()) *);
    template <class, class> static long test(...);
    static const bool value = sizeof(test<T, U>(nullptr)) == 1;
};

int main()
{
    static_assert(!__is_assignable(takes &, both), "");
    static_assert(!__is_trivially_assignable(takes &, both), "");
    static_assert(__is_assignable(takes &, int), "");
    static_assert(!assignable<takes, both>::value, "");
    return 0;
}
