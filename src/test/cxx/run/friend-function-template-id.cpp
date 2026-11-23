/* [temp.friend]/1: friend R f<>(P); names the specialization of the function
   template f that the declared type picks (libc++'s basic_string befriends
   __concatenate_strings<> with parameters __type_identity_t<__self_view>,
   which are non-deduced) and gives it access to the class's privates */
template <class T> struct identity {
  typedef T type;
};
template <class T> using identity_t = typename identity<T>::type;

template <class C> struct view {
  int n;
};
template <class C, class A> struct str;
template <class C, class A>
str<C, A> cat(const A &, identity_t<view<C> > a, identity_t<view<C> > b);

template <class C, class A> struct str {
  typedef view<C> self_view;

public:
  str() : secret_(0) {}

private:
  int secret_;
  friend str cat<>(const A &, identity_t<self_view>, identity_t<self_view>);
};

template <class C, class A>
str<C, A> cat(const A &, identity_t<view<C> > a, identity_t<view<C> > b)
{
  str<C, A> r;
  r.secret_ = a.n + b.n;
  return r;
}

int main()
{
  view<char> x = {2}, y = {3};
  int dummy = 0;
  str<char, int> s = cat<char, int>(dummy, x, y);
  (void)s;
  return 0;
}
