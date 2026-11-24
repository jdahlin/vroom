/* [class.protected]/1: a protected overloaded member function called as
   Base::f(x) in a member of the derived class (the object expression is
   *this, of the derived class, not of the base the member is converted to);
   [temp.inst]/2: a class template specialization named in a class's
   member-specification that has a pending specialization of the class as an
   argument is defined with the class, so that its in-class static
   initializer (Cp::bits) finds the class's members. */

template <class Cp, bool K> struct Iter {
    static const unsigned bits = Cp::bits;
    unsigned get() const { return bits; }
};

template <class T> struct Wrap {
    T value;
};

template <class Cp> struct Pair {
    typedef Wrap<Iter<Cp, false> > type;
};

template <int N, int M> struct Base {
  protected:
    int make(int p) { return p + N; }
    int make(int p) const { return p - N; }
    static const unsigned bits = 64;
    friend struct Iter<Base, false>;
    typedef Iter<Base, false> iterator;
    typedef typename Pair<Base>::type wrapped;

  public:
    iterator begin() { return iterator(); }
};

template <int N> struct Derived : Base<N, 1> {
    typedef Base<N, 1> base;
    int run(int x) { return base::make(x); }
    int run_const(int x) const { return base::make(x); }
    int out_of_class(int x);
};

template <int N> int Derived<N>::out_of_class(int x)
{
    return base::make(x) + this->make(x);
}

int main()
{
    Derived<3> d;
    if (d.run(10) != 13)
        return 1;
    if (d.run_const(10) != 7)
        return 2;
    if (d.out_of_class(1) != 8)
        return 3;
    if (d.begin().get() != 64)
        return 4;
    return 0;
}
