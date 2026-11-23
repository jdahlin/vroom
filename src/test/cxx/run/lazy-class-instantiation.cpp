/* [temp.inst]/2 and /4: a class named as a member function's return type
   needs no definition, and a member function's body is instantiated where it
   is used: every Expr<E> here has a member returning Expr<Neg<E>>, which a
   program that instantiated eagerly would follow without end (libc++'s
   valarray expression templates do) */
struct Leaf {
  int v() const { return 5; }
};
template <class E> struct Neg {
  E e;
  int v() const { return -e.v(); }
};
template <class E> struct Expr {
  E e;
  int v() const { return e.v(); }
  Expr<Neg<E> > negated() const
  {
    Neg<E> n = {e};
    Expr<Neg<E> > r = {n};
    return r;
  }
  Expr<Neg<Neg<E> > > twice() const { return negated().negated(); }
};

int main()
{
  Expr<Leaf> a = {Leaf()};
  if (a.v() != 5)
    return 1;
  if (a.negated().v() != -5)
    return 2;
  if (a.negated().negated().v() != 5)
    return 3;
  if (a.twice().v() != 5)
    return 4;
  return 0;
}
