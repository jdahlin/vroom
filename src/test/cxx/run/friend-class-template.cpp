/* [temp.friend]/1: template <class> friend class X; makes every
   specialization of X a friend (libc++'s slice_array befriends valarray) */
template <class T> class valarray;
template <class T> class slice_array {
  template <class U> friend class valarray;
  int size_;

public:
  slice_array() : size_(3) {}
};
template <class T> class valarray {
public:
  valarray(const slice_array<T> &sa);
  int n;
};
template <class T> valarray<T>::valarray(const slice_array<T> &sa) : n(sa.size_) {}

int main()
{
  slice_array<int> s;
  valarray<int> v(s);
  slice_array<long> t;
  valarray<long> w(t);
  if (w.n != 3)
    return 2;
  if (v.n != 3)
    return 1;
  return 0;
}
