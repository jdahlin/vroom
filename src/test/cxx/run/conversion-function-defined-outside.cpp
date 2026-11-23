/* [class.conv.fct]/1, [basic.lookup.unqual]/8: a conversion function defined
   outside its class, also of a class template, whose conversion-type-id
   names the class by its injected-class-name (libc++'s
   __val_expr<_ValExpr>::operator valarray<__val_expr::__result_type>()); and
   [class.friend]: a class template befriends another that has a private
   copy constructor */
template <class T> struct box {
  T v;
};

struct plain {
  int n;
  operator int() const;
};
plain::operator int() const { return n * 2; }

template <class E> struct expr {
  typedef int result_type;
  E e;
  operator box<result_type>() const;
};
template <class E> expr<E>::operator box<expr::result_type>() const
{
  box<result_type> b = {e + 1};
  return b;
}

template <class T> class vals;
template <class T> class slice {
  template <class U> friend class vals;
  slice(const slice &) {}

public:
  slice() {}
};
template <class T> class vals {
  slice<T> s;

public:
  vals() {}
  vals(const vals &o) : s(o.s) {}
};

int main()
{
  plain p = {4};
  int twice = p;
  if (twice != 8)
    return 1;
  expr<int> x = {4};
  box<int> b = x;
  if (b.v != 5)
    return 2;
  vals<int> a;
  vals<int> c = a;
  (void)c;
  return 0;
}
