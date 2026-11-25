/* [temp.deduct.call]/1: a braced-init-list argument of a member function
   template call, deducing nothing for a class parameter */
struct P {
    int a, b;
};
struct M {
    int base = 100;
    int insert(const P &p)
    {
        return base + p.a * 10 + p.b;
    }
    template <class It> int insert(It first, It last)
    {
        return *first + *last;
    }
    template <class T> static int of(T t, P p)
    {
        return t + p.a + p.b;
    }
};
int main()
{
    M m;
    int x = 3;
    return !(m.insert({x, 2}) == 132 && M::of(1, {2, 3}) == 6 &&
             m.of(x, {x, x}) == 9);
}
