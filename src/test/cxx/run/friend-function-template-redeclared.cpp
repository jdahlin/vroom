/* [temp.friend]/1, [temp.over.link]/7: a friend function template declared
   in a class template, redeclared and defined at namespace scope: one
   template (equivalent heads and function types, a non-type template
   parameter named in the return type), a call to its specialization
   analyses the friend declaration's specifiers */
template <unsigned J, class T>
struct elem {
    typedef T type;
};
template <class... T>
class tup {
  public:
    tup() : base(7) {}

  private:
    int base;
    template <unsigned J, class... U>
    friend typename elem<J, int>::type &get(tup<U...> &) throw();
};
template <unsigned J, class... U>
typename elem<J, int>::type &get(tup<U...> &t) throw()
{
    return t.base;
}
int main()
{
    tup<int, long> t;
    if (get<0>(t) != 7)
        return 1;
    return 0;
}
