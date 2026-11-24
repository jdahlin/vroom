/* [temp.over.link]/7: a function template declared, then defined, with a
   non-type template parameter named in the return type
   (typename elem<I, tup<T...> >::type&) is one template, as libc++'s
   std::get of tuple, forward-declared in <__fwd/tuple.h> */
template <class... T>
class tup;
template <unsigned J, class... T>
struct elem;
template <unsigned J, class... T>
struct elem<J, tup<T...> > {
    typedef int type;
};
template <unsigned I, class... T>
typename elem<I, tup<T...> >::type &get(tup<T...> &) throw();
template <class... T>
class tup {
  public:
    tup() : base(5) {}

  private:
    int base;
    template <unsigned J, class... U>
    friend typename elem<J, tup<U...> >::type &get(tup<U...> &) throw();
};
template <unsigned I, class... T>
typename elem<I, tup<T...> >::type &get(tup<T...> &t) throw()
{
    return t.base;
}
int main()
{
    /* (not called: the specialization's mangled name differs from clang's, a gap;
   unevaluated) */
    tup<int, long> t;
    static_assert(sizeof(get<0>(t)) == sizeof(int), "");
    return 0;
}
