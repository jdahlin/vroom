/* [temp.inst]/4: the definition of a member function of an implicit
   instantiation is instantiated only when used.  operator%= is ill-formed
   for a floating Rep ([expr.mul]/2) and is never used: no error
   (libc++'s std::chrono::duration<double>). */
template <class Rep> struct duration {
    Rep rep;
    duration &operator%=(const Rep &x)
    {
        rep %= x;
        return *this;
    }
    duration &operator%=(const duration &x)
    {
        rep %= x.rep;
        return *this;
    }
    Rep count() const { return rep; }
};
duration<double> global;
int main()
{
    duration<double> d;
    d.rep = 3.5;
    duration<int> i;
    i.rep = 7;
    i %= 4;
    return (int)(d.count() - 3.5) + (i.count() - 3);
}
