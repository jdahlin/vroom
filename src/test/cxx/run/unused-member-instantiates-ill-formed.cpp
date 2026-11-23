/* [temp.inst]/4: a member function of an implicit instantiation is
   instantiated only where used.  A member that is never used may call a
   template that is ill-formed for the class's arguments (fill() on an
   array of const elements, as libc++'s std::array<const T, N>, whose
   fill() calls std::fill_n): the program is well-formed, and no code is
   made for the member or for what it calls.  vcc analyses such bodies
   quietly; the templates they instantiate were analysed as uses, which
   reported a false error (assign_through, not constexpr) or marked the
   template's own declaration ill-formed without a diagnostic
   (fill_through, constexpr: libc++'s __fill_n, 69 of Skia's files) */
template <class P, class U> constexpr void fill_through(P p, U u)
{
    *p = u;
}
template <class P, class U> void assign_through(P p, U u)
{
    *p = u;
}

template <class T, int N> struct array {
    T elements[N];
    constexpr void fill(const T &u)
    {
        for (int i = 0; i < N; i++)
            fill_through(&elements[i], u);
    }
    void assign(const T &u)
    {
        assign_through(&elements[0], u);
    }
    constexpr const T &operator[](int i) const
    {
        return elements[i];
    }
};

/* (the same templates used for a modifiable array, where they are fine) */
array<const unsigned, 3> fib = {{1, 1, 2}};
array<int, 2> mutable_one = {{0, 0}};

int main()
{
    mutable_one.fill(5);
    mutable_one.assign(7);
    if (fib[0] + fib[1] + fib[2] != 4)
        return 1;
    if (mutable_one[0] != 7 || mutable_one[1] != 5)
        return 2;
    return 0;
}
