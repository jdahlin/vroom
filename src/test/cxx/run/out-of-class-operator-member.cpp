/* [temp.mem.func]/1: an operator function of a class template defined
   outside it, its return type the class's own template-id (libc++'s
   list<T, A>::operator=), with const, noexcept, a ref-qualifier, a
   default argument and a typedef-name return type */
template <class T> struct C {
  typedef unsigned long size_type;
  T v;
  C& operator=(const C&);
  C& operator+=(T);
  bool operator==(const C& o) const noexcept;
  T operator()(T d);
  T& operator[](size_type i);
  size_type size() const;
  static size_type count();
};
template <class T> C<T>& C<T>::operator=(const C& o) {
  v = o.v;
  return *this;
}
template <class T> C<T>& C<T>::operator+=(T d) {
  v += d;
  return *this;
}
template <class T> bool C<T>::operator==(const C& o) const noexcept {
  return v == o.v;
}
template <class T> T C<T>::operator()(T d) { return v + d; }
template <class T> T& C<T>::operator[](size_type) { return v; }
template <class T>
typename C<T>::size_type C<T>::size() const { return sizeof(T); }
template <class T> typename C<T>::size_type C<T>::count() { return 7; }
int main()
{
  C<int> a = {5}, b = {0};
  b = a;
  b += 2;
  if (b.v != 7)
    return 1;
  if (!(a == a) || a == b)
    return 2;
  if (b(3) != 10 || b(1) != 8)
    return 3;
  b[0] = 9;
  if (b.v != 9)
    return 4;
  if (a.size() != sizeof(int) || C<int>::count() != 7)
    return 5;
  return 0;
}
