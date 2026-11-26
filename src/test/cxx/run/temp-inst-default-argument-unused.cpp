/* [temp.inst]/12 (C++20 [temp.inst]): a default argument of a function
   template's specialization is checked only where a call uses it: a
   constructor template's `= C()` for a C without a default constructor is
   no error in a call that gives the argument, or when it is only a
   candidate (std::map with json's fifo_map_compare) */
struct Compare {
    int n;
    explicit Compare(int v) : n(v)
    {
    }
};
template <class C> struct Map {
    int n;
    Map(const C &c) : n(c.n)
    {
    }
    template <class It> Map(It first, It last, const C &c = C()) : n(c.n)
    {
        for (; first != last; ++first)
            n += *first;
    }
};

template <class C> static int run()
{
    int a[2] = {1, 2};
    C c(10);
    Map<C> one(c);
    Map<C> two(a, a + 2, c);
    return one.n + two.n;
}

int main()
{
    return !(run<Compare>() == 23);
}
