/* [temp.class.spec]/1, [temp.spec.partial.match]/2: a partial specialization
   declared after the primary template was instantiated for other arguments
   is fine; only an instantiation it would have matched makes the program
   ill-formed (libc++'s common_type<time_point<C, D1>, time_point<C, D2>>
   after common_type<duration, duration>) */
template <class... T> struct ct { static const int v = 0; };
template <class C, class D> struct tp {};
template <class C, class D> struct du {};
int early = ct<int, char>::v + ct<du<int, int>, du<char, long> >::v;
template <class C, class D1, class D2> struct ct<tp<C, D1>, tp<C, D2> > {
  static const int v = 7;
};
template <class C, class D1, class D2> struct ct<du<C, D1>, du<C, D2> > {
  static const int v = 9;
};
int main()
{
  if (early != 0)
    return 1;
  if (ct<tp<int, char>, tp<int, long> >::v != 7)
    return 2;
  if (ct<tp<int, char>, tp<char, long> >::v != 0)
    return 3;
  return 0;
}
